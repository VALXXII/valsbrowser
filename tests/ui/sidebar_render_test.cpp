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
static bool Near(float a, float b) { return std::fabs(a - b) < .01f; }
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
    std::printf("sidebar_render_test: %d checks passed\n", checks);
    return 0;
}
