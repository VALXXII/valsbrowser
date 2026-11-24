// Panneau latéral (#127, variante A finale): the production Chrome painter in its column layout, drawn off screen
// (no window, no WebView2, no profile) at the Paper reference size (artboard 1180 x 740 minus its 1 DIP border:
// client 1178 x 738), for the side-by-side with Paper (design/127-sidebar-evidence) and a few pixel checks.
// States: 01 complete, 02 compact, 07 / 10 a drawer open, bookmarks grid B (20 folded, 40 unfolded, compact 40), the edge
// (hovered, dragged at 300, compact), the annotation mode in both layouts, Annoter greyed.
// The page card itself is a WebView2 window in the app: tools/sidebar/compose.py paints the white card at the page box
// the chrome reports, so the renders read like the boards.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include "../../src/chrome.h"
#include "../../src/profile-icons.h"

using namespace NativeChrome;
static int checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
static bool Near(float a, float b, float e = .01f) { return std::fabs(a - b) < e; }
// A 16 x 16 stand-in favicon (Paper draws letter tiles): a rounded square of one colour.
static std::shared_ptr<const FaviconImage> Tile(unsigned rgb, bool round = false) {
    auto f = std::make_shared<FaviconImage>(); f->width = f->height = 32; f->stride = 32 * 4; f->pixels.assign(32 * 32 * 4, 0);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        const float dx = x + .5f - 16, dy = y + .5f - 16; const float r = round ? 16.f : 8.f;
        const float qx = std::max(std::fabs(dx) - (16 - r), 0.f), qy = std::max(std::fabs(dy) - (16 - r), 0.f);
        const float a = std::clamp(r - std::sqrt(qx * qx + qy * qy) + .5f, 0.f, 1.f);
        unsigned char* p = &f->pixels[(y * 32 + x) * 4];
        p[0] = (unsigned char)((rgb & 255) * a); p[1] = (unsigned char)(((rgb >> 8) & 255) * a); p[2] = (unsigned char)(((rgb >> 16) & 255) * a); p[3] = (unsigned char)(255 * a);
    }
    return f;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const std::wstring root = argv[1], out = argv[2];
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    const std::vector<std::wstring> titles{L"Planète — Le Monde", L"VALXXII/browser · GitHub", L"Boîte de réception (3)", L"Météo Paris — 12°, nuageux", L"Browser Design — Paper"};
    const std::vector<std::shared_ptr<const FaviconImage>> icons{Tile(0xFFFFFF), Tile(0xF0F0F0, true), Tile(0xE24B3B), Tile(0xFFCC33), Tile(0x111111)};
    std::ofstream boxes(std::filesystem::path(out + L"\\boxes.txt"));
    int selected = 0;   // the active row
    auto shot = [&](const wchar_t* name, auto setup, int hover = 0, int focus = 0, int count = 5, bool editing = false) {
        Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome");
        c.sidebar = true; c.keyboardModality = true;
        Design::Accent::Set(0x1A8641); Design::Accent::SetInk(0x6FD087);   // the Travail profile (Paper boards)
        c.SetProfileButton(L"Travail", ProfileIcons::Fill(L"briefcase"), D2D1::ColorF(0x6F / 255.f, 0xD0 / 255.f, 0x87 / 255.f), false);
        c.address = AddressBar::Describe(L"https://lemonde.fr/planete", false);
        c.bandShown = true;
        c.annotateShown = true;   // the annotation mode is in every build Val tries (dev / preview): Annoter is the first tool
        for (int i = 0; i < 5; ++i) { Chrome::BandItem b; b.id = 100 + i; b.title = titles[i]; b.tip = titles[i]; b.image = icons[i]; c.bandItems.push_back(b); }
        std::vector<std::wstring> t(titles); std::vector<std::shared_ptr<const FaviconImage>> f(icons);
        while ((int)t.size() < count) { t.push_back(editing ? std::wstring() : L"Onglet " + std::to_wstring(t.size() + 1)); f.push_back(editing ? nullptr : icons[t.size() % 5]); }   // 06: a new tab page (no title, globe)
        t.resize(count); f.resize(count);
        c.SetFavicons(f); c.sideHome.assign(count, false); c.sideDetails.assign(count, L"github.com");
        setup(c);
        c.Paint(t, selected, L"https://lemonde.fr/planete", editing, hover, focus);
        Check(SUCCEEDED(c.SavePng((out + L"\\" + name + L".png").c_str())), "snapshot");
        const auto& L = c.side;
        const Sidebar::Box P = c.sidebarPopover ? c.sidePop.box : Sidebar::Box{};   // the compact « » » popover (over the page)
        char line[320]; std::snprintf(line, sizeof line, "%ls page %.1f %.1f %.1f %.1f overlay %d column %.1f popover %.1f %.1f %.1f %.1f\n", name, L.page.left, L.page.top, L.page.right, L.page.bottom, c.sidebarOverlay ? 1 : 0, c.sidebarOverlay ? c.sideOver.column : c.sidebarColumn, P.left, P.top, P.right, P.bottom);
        boxes << line;
        return c.SurfacePixel(0, 0);
    };
    // 01 Complet.
    shot(L"01-complet", [](Chrome& c) { (void)c; });
    {   // Checks on the complete layout: the page under the 40 DIP bar, the edge handle along it (it wins the hit test).
        Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (checks)");
        c.sidebar = true; c.sidebarResizable = true; c.bandShown = false; c.address = AddressBar::Describe(L"https://lemonde.fr/", false);
        c.Paint(titles, 0, L"", false, 0, 0);
        Check(c.ContentTop() == 40, "page under the 40 DIP window bar");
        Check(c.Hit(245, 300) == Chrome::SidebarEdgeId && c.Hit(241, 300) != Chrome::SidebarEdgeId && c.Hit(120, 66) == 'L', "edge handle: the column's last 6 DIP along the page");
        c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.Paint(titles, 0, L"", false, 0, 0);
        Check(c.ContentTop() == 32 && c.Hit(37, 300) == Chrome::SidebarEdgeId, "compact: the page under the 32 DIP bar, its edge handle");
    }
    // 02 Compact, 07 / 10 a drawer open from each layout.
    shot(L"02-compact", [](Chrome& c) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; });
    shot(L"07-telechargements", [](Chrome& c) { c.latched = 'D'; c.sidebarReserve = 268; });
    shot(L"10-compact-telechargements", [](Chrome& c) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; c.latched = 'D'; c.sidebarReserve = 268; });
    // Bookmarks grid B (Paper « Favoris · 20 · B », « 40 · B déplié », « compact, 40 »).
    auto bookmarksN = [&](Chrome& c, int n) { c.bandItems.clear(); for (int i = 0; i < n; ++i) { Chrome::BandItem b; b.id = 100 + i; b.title = titles[i % 5]; b.tip = titles[i % 5]; b.image = icons[i % 5]; c.bandItems.push_back(b); } };
    shot(L"fav-20-B", [&](Chrome& c) { bookmarksN(c, 20); });
    shot(L"fav-40-deplie", [&](Chrome& c) { bookmarksN(c, 40); c.sidebarGridOpen = true; });
    shot(L"compact-40", [&](Chrome& c) { bookmarksN(c, 40); c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; });
    // « Gérer les favoris » (Paper « Favoris · panneau / compact · Gérer les favoris (tiroir) »): under the unfolded grid and at
    // the foot of the « » » popover; folders in the grid (tiles 3, 11, 23 like the boards).
    auto withFolders = [&](Chrome& c, int n) { bookmarksN(c, n); for (int i : {2, 10, 22}) if (i < n) { c.bandItems[i].folder = true; c.bandItems[i].image = nullptr; } };
    shot(L"gerer-panneau", [&](Chrome& c) { withFolders(c, 40); c.sidebarGridOpen = true; });
    shot(L"gerer-compact", [&](Chrome& c) { withFolders(c, 40); c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; c.sidebarPopover = true; });
    {   // The row's place and target, in both; a tile dragged onto a folder tile (the folder lit, the dragged tile dimmed).
        Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (manage)");
        c.sidebar = true; c.bandShown = true; withFolders(c, 40); c.sidebarGridOpen = true; c.Paint(titles, 0, L"", false, 0, 0);
        Check(!c.side.manage.Empty() && c.Hit((c.side.manage.left + c.side.manage.right) / 2, (c.side.manage.top + c.side.manage.bottom) / 2) == Chrome::SidebarManageId, "panel: « Gérer les favoris » row under « Réduire »");
        const UINT32 row = c.SurfacePixel(c.side.manage.left + 120, c.side.manage.top + 3);
        Check(((row >> 8) & 255) == 0x24, "row #242424");
        const auto f = c.side.grid.tiles[2], l = c.side.grid.tiles[0];
        Check(c.SideFolderAt((f.left + f.right) / 2, (f.top + f.bottom) / 2, 100) == 2 && c.SideFolderAt((l.left + l.right) / 2, (l.top + l.bottom) / 2, 102) == -1 && c.SideFolderAt((f.left + f.right) / 2, (f.top + f.bottom) / 2, 102) == -1, "a folder tile takes a drop (not a link, not itself)");
        c.sidebarGridOpen = false; c.Paint(titles, 0, L"", false, 0, 0); Check(c.side.manage.Empty() && c.Hit(120, 210) != Chrome::SidebarManageId, "folded: no row");
        c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarPopover = true; c.Paint(titles, 0, L"", false, 0, 0);
        Check(c.sidebarPopover && !c.sidePop.manage.Empty() && c.Hit((c.sidePop.manage.left + c.sidePop.manage.right) / 2, (c.sidePop.manage.top + c.sidePop.manage.bottom) / 2) == Chrome::SidebarManageId, "compact: the row at the popover's foot");
    }
    shot(L"glisser-dossier", [&](Chrome& c) { withFolders(c, 20); c.bandDrag = 0; c.bandDropInto = 2; });
    // Resize (Paper « Redimensionner · 1 bord survolé », « 2 pendant le glisser » at 300, « 4 sous le minimum » compact).
    shot(L"redim-1-survol", [&](Chrome& c) { bookmarksN(c, 10); c.sidebarResizable = true; c.sidebarEdge = 1; });
    shot(L"redim-2-glisser", [&](Chrome& c) { bookmarksN(c, 20); c.sidebarResizable = true; c.sidebarEdge = 2; c.sidebarWidth = c.sidebarColumn = 300; });
    shot(L"redim-4-compact", [&](Chrome& c) { bookmarksN(c, 10); c.sidebarResizable = true; c.sidebarEdge = 2; c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; });
    {   // The line: accent along the page's left edge at 300 (2 DIP just outside it), faded out at its ends.
        Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (edge)");
        c.sidebar = true; c.sidebarResizable = true; c.sidebarEdge = 2; c.sidebarWidth = c.sidebarColumn = 300; c.Paint(titles, 0, L"", false, 0, 0);
        const UINT32 mid = c.SurfacePixel(299, 400), end = c.SurfacePixel(299, 735);
        Check(Near(c.side.page.left, 300) && (mid & 255) > 200 && ((mid >> 16) & 255) < 120, "dragging: the accent line at x 298 .. 300");
        Check((end & 255) < 0x40, "faded out at the bottom end");
        c.sidebarEdge = 1; c.Paint(titles, 0, L"", false, 0, 0); const UINT32 hv = c.SurfacePixel(299, 400);
        Check(std::abs((int)(hv & 255) - 0x6B) <= 3 && std::abs((int)((hv >> 16) & 255) - 0x6B) <= 3, "hovered: #6B6B6B");
    }
    // Annotation (Paper « Annotation · panneau · 1 », « compact · 5 »): Annoter active (profile ink), the page 32 DIP lower
    // under the band (the band, the frame and the bubble are their own windows: not in this render).
    shot(L"annot-1-panneau", [&](Chrome& c) { c.address = AddressBar::Describe(L"http://localhost:5173/", false); c.annotateActive = true; c.annotationBand = 32; });
    shot(L"annot-5-compact", [&](Chrome& c) { c.address = AddressBar::Describe(L"http://localhost:5173/checkout", false); c.annotateActive = true; c.annotationBand = 32; c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; });
    shot(L"x-annoter-grise", [](Chrome& c) { c.annotateDimmed = true; });
    {   // Tools in both layouts, Annoter first.
        Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (tools)");
        c.sidebar = true; c.annotateShown = true; c.Paint(titles, 0, L"", false, 0, 0);
        Check(c.sideTools.size() == 4 && c.sideTools[0] == 'N' && c.Hit((c.side.tools[0].left + c.side.tools[0].right) / 2, (c.side.tools[0].top + c.side.tools[0].bottom) / 2) == 'N', "complete: Annoter is the first of four tools");
        c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.Paint(titles, 0, L"", false, 0, 0);
        Check(c.side.tools.size() == 4 && c.Hit(20, (c.side.tools[0].top + c.side.tools[0].bottom) / 2) == 'N', "compact: Annoter first of four tools");
    }
    {   // #127 parity 3: the strip's open / close / selection motion on the column's rows, through the real painter. Frames:
        // an opening row mid-way (it grows, the rows under it are pushed), settled, a closing row's ghost mid-way.
        Motion::State ms(false); ms.EnablePolish();
        Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (motion)");
        c.sidebar = true; c.SetMotion(&ms); c.animateSelection = true; c.address = AddressBar::Describe(L"https://lemonde.fr/", false);
        std::vector<std::wstring> five(titles.begin(), titles.end()), six = five; six.insert(six.begin() + 2, L"Nouvel article — Le Monde");
        std::vector<std::shared_ptr<const FaviconImage>> f5(icons.begin(), icons.end()), f6 = f5; f6.insert(f6.begin() + 2, Tile(0x3E67FF));
        const auto t0 = Motion::Now(); c.tabKeys = {1, 2, 3, 4, 5}; c.SetFavicons(f5); c.sideHome.assign(5, false); c.sideDetails.assign(5, L"");
        ms.SelectTab(1, t0); ms.selection.Settle(); c.Paint(five, 0, L"", false, 0, 0, nullptr, t0);
        const float pitch = Sidebar::Rows(Sidebar::Mode::Complete).row + Sidebar::Rows(Sidebar::Mode::Complete).gap;
        c.tabKeys = {1, 2, 9, 3, 4, 5}; c.SetFavicons(f6); c.sideHome.assign(6, false); c.sideDetails.assign(6, L"");
        ms.SelectTab(9, t0); c.Paint(six, 2, L"", false, 0, 0, nullptr, t0); c.Paint(six, 2, L"", false, 0, 0, nullptr, t0 + Motion::Milliseconds(90));   // the change, then a frame 90 ms in
        Check(c.sideFrame.items.size() == 6 && c.sideFrame.items[2].presence > .1f && c.sideFrame.items[2].presence < .95f && c.sideFrame.items[3].slot > 2 * pitch && c.sideFrame.items[3].slot < 3 * pitch, "mid-open: the new row grows, the next one pushed down");
        Check(SUCCEEDED(c.SavePng((out + L"\\anim-ouverture-90ms.png").c_str())), "frame: opening");
        c.Paint(six, 2, L"", false, 0, 0, nullptr, t0 + Motion::Milliseconds(1500));
        Check(Near(c.sideFrame.items[3].slot, 3 * pitch) && !c.sideFrame.moving, "settled after the spring");
        Check(SUCCEEDED(c.SavePng((out + L"\\anim-ouverture-fin.png").c_str())), "frame: opened");
        const auto t1 = t0 + Motion::Milliseconds(2000); c.tabKeys = {1, 2, 3, 4, 5}; c.SetFavicons(f5); c.sideHome.assign(5, false); c.sideDetails.assign(5, L"");
        ms.SelectTab(2, t1); c.Paint(five, 1, L"", false, 0, 0, nullptr, t1); c.Paint(five, 1, L"", false, 0, 0, nullptr, t1 + Motion::Milliseconds(60));
        bool ghost = false; for (auto& it : c.sideFrame.items) ghost = ghost || (it.key == 9 && it.index < 0 && it.exit > 0 && it.exit < 1);
        Check(ghost && c.ghostBlurDraws + c.ghostFadeDraws > 0, "closing: the ghost is drawn (blur, or fade without Direct2D 1.1)");
        Check(SUCCEEDED(c.SavePng((out + L"\\anim-fermeture-60ms.png").c_str())), "frame: closing");
        // Spec §1 close: the content fades out in 80 ms, THEN the row folds (.22 s): at 60 ms the slot is still whole.
        bool held = false; for (auto& it : c.sideFrame.items) held = held || (it.key == 9 && it.presence > .99f);
        Check(held, "closing: the slot waits for the content's 80 ms fade before folding");
        // Spec §1 selection: the active background slides from the old row to the new one (one object, spring .25 s).
        const auto t2 = t1 + Motion::Milliseconds(2000); c.Paint(five, 1, L"", false, 0, 0, nullptr, t2);
        const float from = c.selTop.Value(t2); ms.SelectTab(4, t2); c.Paint(five, 3, L"", false, 0, 0, nullptr, t2); c.Paint(five, 3, L"", false, 0, 0, nullptr, t2 + Motion::Milliseconds(70));
        const float mid = c.selTop.Value(t2 + Motion::Milliseconds(70)), to = c.side.tabs[3].top;
        char msg[160]; std::snprintf(msg, sizeof msg, "selection: the plate slides (%.1f -> %.1f -> %.1f)", from, mid, to);
        Check(Near(from, c.side.tabs[1].top) && mid > from + 4 && mid < to - 4, msg);
        Check(SUCCEEDED(c.SavePng((out + L"\\anim-selection-70ms.png").c_str())), "frame: selection");
        c.Paint(five, 3, L"", false, 0, 0, nullptr, t2 + Motion::Milliseconds(1500)); Check(Near(c.selTop.Value(t2 + Motion::Milliseconds(1500)), to), "selection: settled on the new row");
        // Spec §2: folding, the pieces glide from the complete layout to the compact one (morph on the fold's spring).
        const auto t3 = t2 + Motion::Milliseconds(3000); c.bandShown = true; c.bandItems.clear();
        for (int i = 0; i < 5; ++i) { Chrome::BandItem b; b.id = 100 + i; b.title = titles[i]; b.tip = titles[i]; b.image = icons[i]; c.bandItems.push_back(b); }
        c.Paint(five, 3, L"", false, 0, 0, nullptr, t3); const auto tileFrom = c.side.grid.tiles[3], profileFrom = c.side.profile;
        c.sideMorphing = true; c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = 150; c.sidebarTitles = 0;
        c.Paint(five, 3, L"", false, 0, 0, nullptr, t3); const auto tileTo = c.side.tiles[3];
        c.Paint(five, 3, L"", false, 0, 0, nullptr, t3 + Motion::Milliseconds(90));
        const auto& mt = c.sideMorph[BookmarkBase + 3]; const float x = mt.l.Value(t3 + Motion::Milliseconds(90)), y = mt.t.Value(t3 + Motion::Milliseconds(90));
        std::snprintf(msg, sizeof msg, "fold: tile 4 glides (%.0f,%.0f) -> (%.0f,%.0f) -> (%.0f,%.0f)", tileFrom.left, tileFrom.top, x, y, tileTo.left, tileTo.top);
        Check(x < tileFrom.left - 2 && x > tileTo.left + 2 && y > tileFrom.top + 2 && y < tileTo.top - 2, msg);
        Check(c.MorphMoving(t3 + Motion::Milliseconds(90)) && !c.MorphMoving(t3 + Motion::Milliseconds(2000)), "the morph settles");
        Check(SUCCEEDED(c.SavePng((out + L"\\anim-pliage-90ms.png").c_str())), "frame: folding");
        (void)profileFrom; c.sideMorphing = false;
        // Spec §1 « Réordonner »: the lifted row grows to x1.02 (.15 s); the rows it passes step aside (.2 s), no insertion line.
        c.sidebarMode = Sidebar::Mode::Complete; c.sidebarColumn = Sidebar::Complete::width; c.sidebarTitles = 1; c.bandShown = false;
        const auto t4 = t3 + Motion::Milliseconds(3000); c.Paint(five, 0, L"", false, 0, 0, nullptr, t4);
        c.sidebarDrag = 0; c.sidebarInsert = 2; c.sidebarDragY = c.side.tabs[1].top + 6;
        c.Paint(five, 0, L"", false, 0, 0, nullptr, t4); c.Paint(five, 0, L"", false, 0, 0, nullptr, t4 + Motion::Milliseconds(60));
        const float step = c.side.tabs[0].Height() + Sidebar::Rows(Sidebar::Mode::Complete).gap, s1 = c.RowShift(2, t4 + Motion::Milliseconds(60)), g = c.liftGrow.Value(t4 + Motion::Milliseconds(60));
        std::snprintf(msg, sizeof msg, "reorder: rows step aside (%.1f of %.1f), the lifted row grows (%.3f)", s1, -step, g);
        Check(s1 < -1 && s1 > -step && g > 1.001f && g < 1.021f && Near(c.RowShift(4, t4 + Motion::Milliseconds(60)), 0), msg);
        Check(SUCCEEDED(c.SavePng((out + L"\\anim-reordonner-60ms.png").c_str())), "frame: reorder");
        c.Paint(five, 0, L"", false, 0, 0, nullptr, t4 + Motion::Milliseconds(1500)); Check(Near(c.RowShift(2, t4 + Motion::Milliseconds(1500)), -step, .05f) && Near(c.liftGrow.Value(t4 + Motion::Milliseconds(1500)), 1.02f, .001f), "settled: a whole step, x1.02");
        c.sidebarDrag = -1; c.sidebarInsert = -1;
        // Spec §4 « Repère de vue partagée »: a pair made from the menu (tab 5 joins tab 2, the active one): the moved rows
        // slide from their former slots (.3 s), the frame grows from the active row to the block, the line comes once they touch.
        c.Paint(five, 1, L"", false, 0, 0, nullptr, t4 + Motion::Milliseconds(2000)); c.Paint(five, 1, L"", false, 0, 0, nullptr, t4 + Motion::Milliseconds(3000));
        Check(Near(c.RowShift(2, t4 + Motion::Milliseconds(3000)), 0, .05f), "the drag ended without a drop: the stepped-aside rows went home");
        const auto t5 = t4 + Motion::Milliseconds(4000); c.Paint(five, 1, L"", false, 0, 0, nullptr, t5);
        std::vector<std::wstring> moved = {five[0], five[1], five[4], five[2], five[3]}; c.tabKeys = {1, 2, 5, 3, 4}; c.tabGroups = {0, 7, 7, 0, 0};
        c.Paint(moved, 1, L"", false, 0, 0, nullptr, t5); const auto t5b = t5 + Motion::Milliseconds(80); c.Paint(moved, 1, L"", false, 0, 0, nullptr, t5b);
        {   const auto& mk = c.splitMarks[7]; const float sl = c.RowShift(5, t5b), up = c.RowShift(3, t5b), h = mk.b.Value(t5b) - mk.t.Value(t5b);
            const float H = c.side.blocks.empty() ? 0.f : c.side.blocks[0].Height(), rowH = c.side.tabs[1].Height() + 2 * Sidebar::Split::pad;
            std::snprintf(msg, sizeof msg, "§4: rows slide together (%.1f, %.1f), the frame grows from the active row (%.1f of %.1f..%.1f), no line yet", sl, up, h, rowH, H);
            Check(sl > 2 && sl < 2 * pitch && up < -2 && h > rowH + 1 && h < H - 1 && mk.line.Value(t5b) < .01f, msg);
            Check(SUCCEEDED(c.SavePng((out + L"\\anim-paire-80ms.png").c_str())), "frame: pair"); }
        c.Paint(moved, 1, L"", false, 0, 0, nullptr, t5 + Motion::Milliseconds(1500)); c.Paint(moved, 1, L"", false, 0, 0, nullptr, t5 + Motion::Milliseconds(1800));
        {   const auto tz = t5 + Motion::Milliseconds(1800); const auto& mk = c.splitMarks[7];
            Check(Near(c.RowShift(5, tz), 0, .05f) && Near(mk.b.Value(tz) - mk.t.Value(tz), c.side.blocks[0].Height(), .05f) && mk.line.Value(tz) > .99f, "§4: settled, the block and its line"); }
        // Separation: the line fades out, the frame goes back to the row it came from.
        const auto t6 = t5 + Motion::Milliseconds(3000); c.tabGroups.clear(); c.Paint(moved, 1, L"", false, 0, 0, nullptr, t6); c.Paint(moved, 1, L"", false, 0, 0, nullptr, t6 + Motion::Milliseconds(60));
        Check(c.splitMarks.count(7) && c.splitMarks[7].leaving && c.splitMarks[7].line.Value(t6 + Motion::Milliseconds(60)) < .99f, "§4: separated, the line fades out");
        c.Paint(moved, 1, L"", false, 0, 0, nullptr, t6 + Motion::Milliseconds(1500)); c.Paint(moved, 1, L"", false, 0, 0, nullptr, t6 + Motion::Milliseconds(1600));
        Check(!c.splitMarks.count(7), "§4: the frame gone once back on its row");
    }
    {   // Val (24 nov.): the address field is ONE colour while typing: the native EDIT's colour (WM_CTLCOLOREDIT uses
        // Chrome::EditSurface) is the colour painted under it, in the column, the compact bar and the top bar; the EDIT spans
        // the field while typing (Copy / the star are hidden), the address drawn at rest stops before them.
        auto field = [&](bool sidebar, bool compact, const char* what) {
            Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (edit)");
            c.sidebar = sidebar; if (compact) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; }
            c.addressEditing = true; c.address = AddressBar::Describe(L"", false); c.Paint(titles, 0, L"", true, 0, 0);
            const float x = (c.edit.left + c.edit.right) / 2, y = (c.edit.top + c.edit.bottom) / 2;
            const int painted = (c.SurfacePixel(x, y) >> 8) & 255, edit = (int)std::lround(c.EditSurface() * 255);
            char msg[160]; std::snprintf(msg, sizeof msg, "%s: field under the EDIT %d = EDIT colour %d", what, painted, edit);
            Check(std::abs(painted - edit) <= 1, msg);
            return c.edit;
        };
        {   // Val #5: the address outline while typing is white 5 % after a click (50 % from the keyboard); #6: the fold
            // button's glyph at rest is white 50 % like Back / Forward / Reload.
            Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (ring)");
            c.sidebar = true; c.addressEditing = true; c.keyboardModality = false; c.address = AddressBar::Describe(L"", false); c.Paint(titles, 0, L"", true, 0, 0);
            const auto a = c.side.address; const int inside = (c.SurfacePixel((a.left + a.right) / 2, a.top + 6) >> 8) & 255, ring = (c.SurfacePixel((a.left + a.right) / 2, a.top + .5f) >> 8) & 255;
            char msg[160]; std::snprintf(msg, sizeof msg, "address outline after a click: white 5 %% (ring %d over field %d)", ring, inside);
            Check(ring > inside && ring - inside <= 15, msg);
            c.keyboardModality = true; c.Paint(titles, 0, L"", true, 0, 0);
            const int keyed = (c.SurfacePixel((a.left + a.right) / 2, a.top + .5f) >> 8) & 255; Check(keyed - inside > 60, "from the keyboard the outline keeps 50 % (accessibility)");
            c.addressEditing = false; c.Paint(titles, 0, L"", false, 0, 0); int fold = 0; const auto t = c.side.toggle;
            for (float y = t.top; y < t.bottom; ++y) for (float x = t.left; x < t.right; ++x) fold = std::max(fold, (int)((c.SurfacePixel(x, y) >> 8) & 255));
            std::snprintf(msg, sizeof msg, "fold button glyph at 50 %% (brightest %d, ~139)", fold); Check(fold > 110 && fold < 160, msg);
        }
        // Val #5, both cases in the three layouts: after a click white 5 % (#FFFFFF0D), from the keyboard the 50 % ring.
        for (int layout = 0; layout < 3; ++layout) {
            const char* name = layout == 0 ? "top bar" : layout == 1 ? "column" : "compact bar";
            Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (rings)");
            c.sidebar = layout != 0; if (layout == 2) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; }
            c.addressEditing = true; c.address = AddressBar::Describe(L"", false);
            auto sample = [&](bool keyboard) { c.keyboardModality = keyboard; c.Paint(titles, 0, L"", true, 0, 'L');
                const D2D1_RECT_F a = c.sidebar ? D2D1_RECT_F{c.side.address.left, c.side.address.top, c.side.address.right, c.side.address.bottom} : c.bar;
                const float x = a.left + 40; return std::make_pair((int)((c.SurfacePixel(x, a.top + 6) >> 8) & 255), (int)((c.SurfacePixel(x, a.top + .5f) >> 8) & 255)); };
            const auto click = sample(false), keys = sample(true); char msg[200];
            std::snprintf(msg, sizeof msg, "%s: outline after a click %d over %d (white 5 %%), from the keyboard %d (50 %%)", name, click.second, click.first, keys.second);
            Check(click.second > click.first && click.second - click.first <= 15 && keys.second - keys.first > 60, msg);
        }
        const auto col = field(true, false, "column");
        Check(Near(col.right, 248 - 8 - 8), "column: the EDIT spans the field while typing (to its padding)");
        field(true, true, "compact bar");
        field(false, false, "top bar");
    }
    // Vue partagée dans la colonne (Paper « Proposition — Vue partagée dans la colonne (panneau + compact) » 2473-0), with the
    // board's tabs: « 1 · Repos » (YouTube and Paper under one background, YouTube displayed) and « 2 · Survol du menu »
    // (right click on Paper, « À droite » highlighted: YouTube and Paper outlined), panel and compact. split-boxes.txt gives
    // tools/sidebar/compose_split.py the first row (the boards' rows start 9 into the panel, 5 into the compact column).
    {
        std::ofstream splitBoxes(std::filesystem::path(out + L"\\split-boxes.txt"));
        const std::vector<std::wstring> rest{L"Boutique — paiement", L"YouTube — Montage rapide", L"Browser Design — Paper", L"Boîte de réception (3)", L"Météo Paris — 12°"};
        const std::vector<std::shared_ptr<const FaviconImage>> restIcons{Tile(0xFFFFFF), Tile(0xE5322D), Tile(0x111111), Tile(0xE24B3B), Tile(0xFFCC33)};
        auto splitShot = [&](const wchar_t* name, bool compact, bool hovered) {
            Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (split)");
            c.sidebar = true; c.keyboardModality = true; c.bandShown = false; c.annotateShown = true;
            c.address = AddressBar::Describe(L"https://youtube.com/", false);
            std::vector<std::wstring> t = rest; std::vector<std::shared_ptr<const FaviconImage>> f = restIcons;
            if (hovered) { std::swap(t[2], t[3]); std::swap(f[2], f[3]); }   // 248W-0: Boutique, YouTube, Boîte, Paper, Météo
            c.SetFavicons(f); c.sideHome.assign(5, false); c.sideDetails.assign(5, L"youtube.com");
            if (compact) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; }
            if (hovered) { c.menuTargets = {1}; c.menuSource = 3; } else { c.tabGroups = {0, 7, 7, 0, 0}; c.visibleTabGroup = 7; }
            { const auto t0 = Motion::Now(); c.Paint(t, 1, L"https://youtube.com/", false, 0, 0, nullptr, t0); c.Paint(t, 1, L"https://youtube.com/", false, 0, 0, nullptr, t0 + Motion::Milliseconds(200)); }   // §4: past the menu outline's 100 ms fade
            Check(SUCCEEDED(c.SavePng((out + L"\\" + name + L".png").c_str())), "snapshot (split)");
            const auto& L = c.side; char line[200];
            std::snprintf(line, sizeof line, "%ls %.1f %.1f %.1f %.1f\n", name, L.tabs[0].left, L.tabs[0].top, L.tabs[4].right, L.tabs[4].bottom); splitBoxes << line;
            return L;
        };
        auto grey = [](Chrome& c, float x, float y) { return (int)((c.SurfacePixel(x, y) >> 8) & 255); };
        splitShot(L"split-1-repos-panneau", false, false); splitShot(L"split-1-repos-compact", true, false);
        splitShot(L"split-2-survol-panneau", false, true); splitShot(L"split-2-survol-compact", true, true);
        // « 1 · Repos »: the background, the line, the displayed tab active and the other normal on the background.
        for (int compact = 0; compact < 2; ++compact) {
            Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (split checks)");
            c.sidebar = true; c.bandShown = false; c.tabGroups = {0, 7, 7, 0, 0};
            if (compact) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; }
            c.Paint(titles, 1, L"", false, 0, 0); const auto& L = c.side;
            Check(L.blocks.size() == 1 && L.joints.size() == 1, "split: one block, one line");
            const auto b = L.blocks[0], j = L.joints[0]; const float mid2 = (L.tabs[2].top + L.tabs[2].bottom) / 2;
            { char m2[200]; std::snprintf(m2, sizeof m2, "%s: the block is a 1 DIP #262626 outline (%d at %.1f), no fill (%d) (frozen 2473-0)", compact ? "compact" : "panel", grey(c, b.left + (compact ? .5f : -.5f), mid2), b.left + (compact ? .5f : -.5f), grey(c, b.left + (compact ? 1.5f : 1.f), mid2));
              Check(std::abs(grey(c, b.left + (compact ? .5f : -.5f), mid2) - 0x26) <= 2 && std::abs(grey(c, b.left + (compact ? 1.5f : 1.f), mid2) - 0x17) <= 1, m2); }
            Check(std::abs(grey(c, (j.left + j.right) / 2, j.top) - 0x54) <= 2, compact ? "compact: the 10 x 1 line #545454" : "panel: the 10 x 1 line #545454 on the favicons' axis");
            { char m3[160]; std::snprintf(m3, sizeof m3, "the displayed pair: both rows on #262626 (%d, %d), no #3E3E3E of their own", grey(c, L.tabs[1].left + 3, (L.tabs[1].top + L.tabs[1].bottom) / 2), grey(c, L.tabs[2].left + 3, mid2));
              Check(std::abs(grey(c, L.tabs[1].left + 3, (L.tabs[1].top + L.tabs[1].bottom) / 2) - 0x26) <= 1 && std::abs(grey(c, L.tabs[2].left + 3, mid2) - 0x26) <= 1, m3); }
            c.Paint(titles, 0, L"", false, 0, 0); Check(std::abs(grey(c, c.side.tabs[2].left + 3, (c.side.tabs[2].top + c.side.tabs[2].bottom) / 2) - 0x17) <= 1 && std::abs(grey(c, c.side.blocks[0].left + (compact ? .5f : -.5f), (c.side.tabs[2].top + c.side.tabs[2].bottom) / 2) - 0x26) <= 2, "at rest (the pair not displayed): rows normal, the outline stays");
            Check(std::abs(grey(c, b.left + 1, b.bottom + 1) - 0x17) <= 1, "under the block: the canvas (the 2 DIP gap)");
            Check(c.Hit((L.tabs[2].left + L.tabs[2].right) / 2, mid2) == TabBase + 2 && c.Hit((L.tabs[0].left + L.tabs[0].right) / 2, (L.tabs[0].top + L.tabs[0].bottom) / 2) == TabBase + 0, "rows in and out of the block still hit");
        }
        // « 2 · Survol du menu »: a 1 DIP inner #8C8C8C stroke on the target and on the right-clicked tab, nothing else.
        for (int compact = 0; compact < 2; ++compact) {
            Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (split menu)");
            c.sidebar = true; c.bandShown = false;
            if (compact) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; }
            auto edge = [&](int i, float dx) { const auto& r = c.side.tabs[i]; return grey(c, r.left + dx, (r.top + r.bottom) / 2); };
            c.Paint(titles, 1, L"", false, 0, 0);
            Check(std::abs(edge(3, 0) - 0x17) <= 1 && std::abs(edge(1, 0) - 0x3E) <= 1, "no designation: no outline");
            c.menuSource = 3; c.Paint(titles, 1, L"", false, 0, 0);
            Check(std::abs(edge(3, 0) - 0x17) <= 1, "the right-clicked tab alone is not outlined");
            // Motion spec §4: the outline fades in over 100 ms (and out the same way).
            const auto t0 = Motion::Now(); c.menuTargets = {1}; c.Paint(titles, 1, L"", false, 0, 0, nullptr, t0);
            Check(std::abs(edge(1, 0) - 0x3E) <= 2 && std::abs(edge(3, 0) - 0x17) <= 2, "§4: the outline starts invisible");
            c.Paint(titles, 1, L"", false, 0, 0, nullptr, t0 + std::chrono::milliseconds(50));
            Check(edge(3, 0) > 0x17 + 8 && edge(3, 0) < 0x8C - 8, "§4: half-way through its 100 ms fade");
            c.Paint(titles, 1, L"", false, 0, 0, nullptr, t0 + std::chrono::milliseconds(150));
            Check(std::abs(edge(1, 0) - 0x8C) <= 2 && std::abs(edge(3, 0) - 0x8C) <= 2, compact ? "compact: target and right-clicked tab outlined #8C8C8C" : "panel: target and right-clicked tab outlined #8C8C8C");
            Check(std::abs(edge(1, 1) - 0x3E) <= 1 && std::abs(edge(3, 1) - 0x17) <= 1, "1 DIP, inside the row");
            Check(std::abs(edge(0, 0) - 0x17) <= 1 && std::abs(edge(2, 0) - 0x17) <= 1 && std::abs(edge(4, 0) - 0x17) <= 1, "the other rows: no outline");
            const auto t1 = t0 + std::chrono::milliseconds(300); c.menuTargets.clear(); c.menuSource = -1; c.Paint(titles, 1, L"", false, 0, 0, nullptr, t1);
            Check(std::abs(edge(3, 0) - 0x8C) <= 2, "§4: the menu closed, the outline fades out (not cut)");
            c.Paint(titles, 1, L"", false, 0, 0, nullptr, t1 + std::chrono::milliseconds(150));
            Check(std::abs(edge(1, 0) - 0x3E) <= 1 && std::abs(edge(3, 0) - 0x17) <= 1, "the menu closed: the outline goes");
            // §4: a new pair (here without motion: reduced, in place) — its 10 x 1 line fades in over 120 ms.
            const auto t2 = t1 + std::chrono::seconds(1); c.tabGroups = {0, 7, 7, 0, 0}; c.Paint(titles, 1, L"", false, 0, 0, nullptr, t2);
            const auto lineAt = [&]() { const auto f = Sidebar::FaviconIn(c.side.tabs[1], c.side.mode); return grey(c, (f.left + f.right) / 2, c.side.tabs[1].bottom + .5f); };
            const int l0 = lineAt(); c.Paint(titles, 1, L"", false, 0, 0, nullptr, t2 + std::chrono::milliseconds(20)); c.Paint(titles, 1, L"", false, 0, 0, nullptr, t2 + std::chrono::milliseconds(200));
            Check(std::abs(l0 - 0x17) <= 2 && std::abs(lineAt() - 0x54) <= 2, compact ? "§4 compact: the new pair's line fades in (120 ms)" : "§4 panel: the new pair's line fades in (120 ms)");
        }
    }
    std::printf("sidebar_render_test: %d checks passed\n", checks);
    return 0;
}
