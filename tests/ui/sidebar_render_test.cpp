// Panneau latéral (#127, variante A finale): the production Chrome painter in its column layout, drawn off screen
// (no window, no WebView2, no profile) at the Paper reference size (artboard 1180 x 740 minus its 1 DIP border:
// client 1178 x 738), for the side-by-side with Paper (design/127-sidebar-evidence) and a few pixel checks.
// States: 01 complete, 02 compact, 03 compact with the complete column over the page, 04 hover + keyboard focus,
// 05 tab drag, 07 Downloads drawer open (its tool latched), plus 2 grid rows and a long list (scroll fades).
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
static int Gray(UINT32 p) { return (p >> 8) & 255; }
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
        char line[256]; std::snprintf(line, sizeof line, "%ls page %.1f %.1f %.1f %.1f overlay %d column %.1f\n", name, L.page.left, L.page.top, L.page.right, L.page.bottom, c.sidebarOverlay ? 1 : 0, c.sidebarOverlay ? c.sideOver.column : c.sidebarColumn);
        boxes << line;
        return c.SurfacePixel(0, 0);
    };
    // 01 Complet.
    shot(L"01-complet", [](Chrome& c) { (void)c; });
    {   // Pixel checks on 01: canvas, active row #3E3E3E, field #292929.
        Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (checks)");
        c.sidebar = true; c.bandShown = false; c.address = AddressBar::Describe(L"https://lemonde.fr/", false);
        c.Paint(titles, 0, L"", false, 0, 0);
        Check(Gray(c.SurfacePixel(120, 300)) == 0x17, "canvas under the column");
        Check(Gray(c.SurfacePixel(200, 112)) == 0x3E, "active tab row #3E3E3E (no grid: first row at y 103)");
        Check(Gray(c.SurfacePixel(180, 56)) >= 0x28 && Gray(c.SurfacePixel(180, 56)) <= 0x2A, "address field #292929");
        Check(c.regions.size() > 10 && c.Hit(20, 115) == TabBase + 0 && c.Hit(120, 66) == 'L' && c.Hit(226, 24) == Chrome::SidebarToggleId, "regions: tab, field, fold button");
        Check(c.ContentTop() == 40, "page under the 40 DIP window bar");
    }
    // 02 Compact.
    shot(L"02-compact", [](Chrome& c) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; });
    // 03 Passage: compact, the complete column over the page.
    shot(L"03-passage", [](Chrome& c) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; c.sidebarOverlay = true; });
    // 04 Survol (row 2, GitHub tile, History tool) + keyboard focus on row 3.
    shot(L"04-survol-focus", [](Chrome& c) { (void)c; }, TabBase + 1, TabBase + 2);
    shot(L"04b-survol-favori", [](Chrome& c) { (void)c; }, BookmarkBase + 1, 0);
    shot(L"04c-survol-outil", [](Chrome& c) { (void)c; }, 'Y', 0);
    // 05 Glisser: row 4 dragged up, landing after row 1.
    shot(L"05-glisser", [](Chrome& c) { c.sidebarDrag = 3; c.sidebarDragY = 186; c.sidebarInsert = 1; });
    // 06 Nouvel onglet: the field in editing.
    selected = 5;   // 06 (corrected): the new tab is a real row (globe, « Nouvel onglet ») after the others, active
    shot(L"06-nouvel-onglet", [](Chrome& c) { c.addressEditing = true; c.sideHome.back() = true; }, 0, 'L', 6, true);
    selected = 0;
    // 07 Téléchargements: the tool latched while its drawer is open.
    shot(L"07-telechargements", [](Chrome& c) { c.latched = 'D'; });
    shot(L"10-compact-telechargements", [](Chrome& c) { c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; c.latched = 'D'; });
    // Two grid rows with « » », and a long list scrolled to its middle (fades).
    shot(L"x-grille-2-rangees", [&](Chrome& c) { for (int i = 5; i < 14; ++i) { Chrome::BandItem b; b.id = 100 + i; b.title = L"Favori"; b.tip = L"Favori"; b.image = icons[i % 5]; c.bandItems.push_back(b); } });
    shot(L"x-liste-longue", [](Chrome& c) { c.sidebarScroll = 200; }, 0, 0, 30);
    shot(L"x-toujours-vide", [](Chrome& c) { c.bandItems.clear(); });
    // Annoter where the mode cannot start (a live site with #125, the new tab page…): greyed, still there; and the mode on.
    shot(L"x-annoter-grise", [](Chrome& c) { c.annotateDimmed = true; });
    shot(L"x-annoter-actif", [](Chrome& c) { c.annotateActive = true; });
    {   // Four tools in both modes, Annoter first (Paper 01 1Q0B-0 / 02 1Q3U-0), at 70 % / greyed.
        Chrome c; Check(SUCCEEDED(c.InitOffscreen(root, 96, 1178, 738)), "offscreen chrome (tools)");
        c.sidebar = true; c.annotateShown = true; c.Paint(titles, 0, L"", false, 0, 0);
        Check(c.sideTools.size() == 4 && c.sideTools[0] == 'N' && c.Hit(36, 714) == 'N', "complete: Annoter is the first of four tools");
        int lit = 0; for (int y = 706; y < 722; ++y) for (int x = 28; x < 44; ++x) lit = std::max(lit, Gray(c.SurfacePixel((float)x, (float)y)));
        Check(lit > 0x17 + (int)((255 - 0x17) * .6f), "Annoter's mark at about 70 % white");
        c.annotateDimmed = true; c.Paint(titles, 0, L"", false, 0, 0); int dim = 0; for (int y = 706; y < 722; ++y) for (int x = 28; x < 44; ++x) dim = std::max(dim, Gray(c.SurfacePixel((float)x, (float)y)));
        Check(dim > 0x17 + 30 && dim < lit - 40, "greyed Annoter stays visible, dimmer");
        c.sidebarMode = Sidebar::Mode::Compact; c.sidebarColumn = Sidebar::Compact::width; c.annotateDimmed = false; c.Paint(titles, 0, L"", false, 0, 0);
        Check(c.side.tools.size() == 4 && c.Hit(28, (c.side.tools[0].top + c.side.tools[0].bottom) / 2) == 'N', "compact: Annoter first of four tools");
    }
    std::printf("sidebar_render_test: %d checks passed\n", checks);
    return 0;
}
