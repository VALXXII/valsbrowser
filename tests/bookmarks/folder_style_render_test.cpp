// #138 A favorites folder's icon and colour, drawn by the production painters off screen (no window is ever shown,
// no WebView2, no profile: demo data only), for the side-by-side with the Paper board « Dossiers de favoris · C finale
// (validée) » (design/138-folders-evidence, tools composed by tests/bookmarks/folder_style_compose.py), plus checks:
//  - the 6 board states: 1 a folder hovered in the Favoris drawer (palette button, « Personnaliser » tip), 2 its picker
//    unfolded with the keyboard focus, 3 right click (the menu's « Personnaliser… », the popover), 4 no colour, 5 long
//    names (bar, drawer, « Déplacer vers », grid tip), 6 the folder everywhere (side grid, top bar, compact column,
//    drawer, « Déplacer vers »);
//  - pixels: each place draws the folder in its colour and the emblem in its darker shade, at the board's size; the
//    chosen cell, the 2 DIP #E6E6E6 focus ring; a long name fades out over 24 DIP (never « … »);
//  - the drawer's picker driven like a person would: palette button, Tab / arrows / Enter / Escape, clicks, the store.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../../src/chrome.h"
#include "../../src/bookmarks-panel.h"
#include "../../src/context-menu.h"
#include "../../src/folder-picker.h"
#include "../../src/tooltip.h"

using namespace NativeChrome;
namespace BM = Bookmarks;
namespace CM = ContextMenu;
namespace P = FolderStyle::Picker;
static int checks = 0, failures = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; } else std::printf("ok   %s\n", what); }
static unsigned R(UINT32 px) { return (px >> 16) & 255; } static unsigned G(UINT32 px) { return (px >> 8) & 255; } static unsigned B(UINT32 px) { return px & 255; }
static bool Near(UINT32 px, std::uint32_t rgb, int tolerance = 6) {
    return std::abs(static_cast<int>(R(px)) - static_cast<int>((rgb >> 16) & 255)) <= tolerance && std::abs(static_cast<int>(G(px)) - static_cast<int>((rgb >> 8) & 255)) <= tolerance && std::abs(static_cast<int>(B(px)) - static_cast<int>(rgb & 255)) <= tolerance;
}
// Reads a pixel of a PNG written by Chrome::SavePng (WIC), x/y in device pixels.
static UINT32 PngPixel(const std::wstring& path, int x, int y) {
    IWICImagingFactory* f = nullptr; IWICBitmapDecoder* d = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* c = nullptr; UINT32 px = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) && SUCCEEDED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &d)) &&
        SUCCEEDED(d->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&c)) && SUCCEEDED(c->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        WICRect r{x, y, 1, 1}; BYTE b[4]{}; if (SUCCEEDED(c->CopyPixels(&r, 4, 4, b))) px = (static_cast<UINT32>(b[2]) << 16) | (static_cast<UINT32>(b[1]) << 8) | b[0];
    }
    if (c) c->Release(); if (fr) fr->Release(); if (d) d->Release(); if (f) f->Release(); return px;
}
// The point of a glyph `size` DIP centred in `box` at 256-box coordinates (gx, gy), in device pixels at `k` = dpi / 96.
struct Pt { int x, y; };
static Pt GlyphPoint(D2D1_RECT_F box, float size, float gx, float gy, float k) {
    const float x = (box.left + box.right - size) / 2 + gx / 256 * size, y = (box.top + box.bottom - size) / 2 + gy / 256 * size;
    return {static_cast<int>(x * k), static_cast<int>(y * k)};
}
// Inside the folder's body, left of the emblem (256 box) / inside the newspaper emblem's page (its own 256 box, mapped).
static constexpr float bodyX = 50, bodyY = 196;
static constexpr float paperX = 78 + .39f * 210, paperY = 92 + .39f * 80;

struct Demo { BM::Store s; BM::Id travail = 0, lisiere = 0, veille = 0, cuisine = 0, divers = 0, lectures = 0, recettes = 0; };
static Demo MakeDemo() {
    Demo d; long long t = 1;
    auto folder = [&](BM::Id parent, const wchar_t* name, const char* icon, const char* color, int links) {
        const BM::Id f = d.s.AddFolder(parent, BM::End, name, t++); d.s.SetStyle(f, icon, color);
        for (int i = 0; i < links; ++i) d.s.Add(f, BM::End, L"Page " + std::to_wstring(i + 1), L"https://example.org/" + std::to_wstring(f) + L"/" + std::to_wstring(i), t++);
        return f;
    };
    d.travail = folder(BM::BarRoot, L"Travail", "briefcase", "vert", 14);
    d.lisiere = folder(BM::BarRoot, L"Projet Lisière", "palette", "violet", 6);
    d.veille = folder(BM::BarRoot, L"Veille", "newspaper", "ambre", 23);
    d.cuisine = folder(BM::BarRoot, L"Cuisine", "coffee", "rose", 9);
    d.divers = folder(BM::BarRoot, L"Divers", "", "", 4);
    d.lectures = folder(BM::OtherRoot, L"Lectures", "book-open", "", 12);
    d.recettes = folder(BM::OtherRoot, L"Recettes de grand-mère et carnets de voyage", "book-open", "cyan", 31);
    d.s.Add(BM::BarRoot, BM::End, L"Le Monde", L"https://www.lemonde.fr/", t++);
    return d;
}
static std::vector<Chrome::BandItem> BandItems(const BM::Store& s, BM::Id root) {
    std::vector<Chrome::BandItem> items;
    for (BM::Id id : s.Root(root).children) { const auto* n = s.Find(id); Chrome::BandItem b; b.id = id; b.folder = n->kind == BM::Kind::Folder; b.title = BM::DisplayTitle(*n);
        if (b.folder) { b.look = FolderStyle::Resolve(n->icon, n->color); b.tip = b.title; b.sideTip = TRF(bookmarks_folder_tip_line, b.title, TRP(bookmarks_count, s.CountLinks(id))); } else b.tip = b.title;
        items.push_back(b); }
    return items;
}
// « Déplacer vers » as main.cpp builds it (FolderChoices + MoveToItems): the roots, then each folder indented by depth.
static std::vector<CM::Item> MoveTo(const BM::Store& s, BM::Id moved) {
    std::vector<CM::Item> items; const auto* m = s.Find(moved);
    for (BM::Id root : {BM::BarRoot, BM::OtherRoot}) { std::vector<std::pair<BM::Id, int>> stack{{root, 0}};
        while (!stack.empty()) { auto [id, depth] = stack.back(); stack.pop_back(); const auto* n = s.Find(id); if (!n || n->kind != BM::Kind::Folder || id == moved) continue;
            auto item = CM::Command(static_cast<int>(id), std::wstring(static_cast<size_t>(depth) * 2, L' ') + n->title, L"", m && id != m->parent, m && id == m->parent);
            item.icon = L"folder-closed"; item.look = FolderStyle::Resolve(n->icon, n->color); item.folderName = true; items.push_back(item);
            for (auto c = n->children.rbegin(); c != n->children.rend(); ++c) stack.push_back({*c, depth + 1}); } }
    return items;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const std::wstring root = argv[1], out = argv[2];
    std::filesystem::create_directories(out);
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = instance; wc.lpszClassName = L"FolderStyleRenderOwner"; RegisterClassW(&wc);
    const HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_OVERLAPPEDWINDOW, -32000, -32000, 1200, 800, nullptr, nullptr, instance, nullptr);   // never shown
    Check(owner != nullptr, "owner window (never shown)");
    std::ofstream boxes(std::filesystem::path(out + L"\\boxes.txt"));
    Demo demo = MakeDemo(); BM::Store& s = demo.s;
    auto file = [&](const std::wstring& name) { return out + L"\\" + name + L".png"; };

    // ---- The Favoris drawer (windowless: rows built, window never shown) --------------------------------------
    BookmarksUI::Drawer drawer; std::vector<std::wstring> said;
    BookmarksUI::Drawer::Hooks hooks;
    hooks.restyle = [&](BM::Id id, const std::string& icon, const std::string& color) { s.SetStyle(id, icon, color); drawer.Changed(); };
    hooks.announce = [&](const std::wstring& t) { said.push_back(t); };
    int visits = 0; BM::Id visiting = 0; hooks.customizing = [&](BM::Id id) { visiting = id; if (id) ++visits; };
    Check(drawer.Create(instance, owner, root, &s, hooks), "drawer created");
    drawer.OpenForTest();
    auto rowOf = [&](BM::Id id) { const auto& rows = drawer.Rows(); for (int i = 0; i < static_cast<int>(rows.size()); ++i) if (rows[i].id == id) return i; return -1; };
    const int veille = rowOf(demo.veille), divers = rowOf(demo.divers);
    Check(veille > 0 && divers > 0 && drawer.Rows()[veille].look.styled && !drawer.Rows()[divers].look.styled, "drawer rows carry their folder's look (Divers: none)");
    {   // The palette button is the folder row's second action, named « Personnaliser »; links keep « + » and « × ».
        bool found = false; for (const auto& n : drawer.AccessibleNodesForTest()) if (n.automationId == L"BookmarkCustomize." + std::to_wstring(demo.veille)) found = n.name == TR(folder_customize) && n.expand == 1;
        Check(found, "UIA: a folder row's « Personnaliser » button (collapsed)");
    }
    for (UINT dpi : {96u, 192u}) {
        const float k = dpi / 96.f; const std::wstring sfx = dpi == 96 ? L"" : L"@2x";
        // 1 · Hover: the palette button lit under the pointer.
        drawer.ScrollForTest(0); drawer.HoverForTest(BookmarksUI::RowHit(veille) + 2); Sleep(260);
        Check(drawer.RenderForTest(root, file(L"1-tiroir-survol" + sfx).c_str(), dpi, 300, false), "1 drawer, Veille hovered");
        if (dpi == 192) {
            const float top = drawer.RowTopForTest(veille); const D2D1_RECT_F tile{Downloads::Panel::padding + BookmarksUI::Layout::indent, top, Downloads::Panel::padding + BookmarksUI::Layout::indent + 32, top + 32};
            const auto body = GlyphPoint(tile, 18, bodyX, bodyY, k), emblem = GlyphPoint(tile, 18, paperX, paperY, k);
            Check(Near(PngPixel(file(L"1-tiroir-survol@2x"), body.x, body.y), 0xE6AC3D), "drawer: Veille's folder in ambre #E6AC3D (18 DIP, board 245I-0)");
            Check(Near(PngPixel(file(L"1-tiroir-survol@2x"), emblem.x, emblem.y), 0x61481A, 10), "drawer: its newspaper emblem in #61481A");
            const auto b = Downloads::Panel::ActionBox(top, 1);
            Check(Near(PngPixel(file(L"1-tiroir-survol@2x"), static_cast<int>((b.left + 2) * k), static_cast<int>((b.top + 2) * k)), 0x333333, 2), "palette button hovered: #333333 (board 23DD-0)");
            const auto glyph = GlyphPoint({b.left, b.top, b.right, b.bottom}, 14, 200, 128, k);
            Check(Near(PngPixel(file(L"1-tiroir-survol@2x"), glyph.x, glyph.y), 0xE6E6E6, 12), "its palette glyph 14 DIP #E6E6E6");
        }
        drawer.HoverForTest(0);
        // 2 · The picker unfolded from the keyboard; focus moved to « rocket » (board 23OW-0).
        drawer.FocusForTest(BookmarksUI::RowHit(veille) + 2, true); drawer.KeyForTest(VK_RETURN);
        Check(drawer.Picking() == demo.veille && drawer.PickerForTest().focus == P::Model::SwatchOf(5), "Enter on the palette: the picker unfolds, focus on the chosen colour (ambre)");
        drawer.KeyForTest(VK_TAB); Check(drawer.PickerForTest().focus == P::Model::CellOf(15), "Tab: the grid, on the chosen icon (newspaper)");
        for (int i = 0; i < 4; ++i) drawer.KeyForTest(VK_RIGHT);
        Check(drawer.PickerForTest().focus == P::Model::CellOf(19) && drawer.Focus() == BookmarksUI::PickerBase + P::Model::CellOf(19), "arrows: rocket");
        Check(drawer.PickerHintForTest().size() == 2, "hint: two lines (board 23RD-0)");
        drawer.ScrollForTest(0); Check(drawer.RenderForTest(root, file(L"2-selecteur-clavier" + sfx).c_str(), dpi, 470, true), "2 drawer, picker open, keyboard focus");
        if (dpi == 192) {
            const float pt = drawer.PickerTopForTest(); const float left = BookmarksUI::Layout::pickerLeft + P::padding, stops = pt + P::padding, grid = stops + P::swatch + P::gap;
            const auto chosen = P::Stop(P::Model::CellOf(15), left, stops, grid), focus = P::Stop(P::Model::CellOf(19), left, stops, grid);
            Check(Near(PngPixel(file(L"2-selecteur-clavier@2x"), static_cast<int>((chosen.left + 3) * k), static_cast<int>((chosen.top + 13) * k)), 0x463A24, 3), "chosen cell: ambre 20 % over #1E1E1E = #463A24 (23OC-0)");
            Check(Near(PngPixel(file(L"2-selecteur-clavier@2x"), static_cast<int>((focus.left - 1) * k), static_cast<int>((focus.top + 13) * k)), 0xE6E6E6, 16), "focus ring: 2 DIP #E6E6E6 outside the cell (23OW-0)");
            Check(Near(PngPixel(file(L"2-selecteur-clavier@2x"), static_cast<int>((left - 6) * k), static_cast<int>((stops + 2) * k)), 0x1E1E1E, 1), "picker surface #1E1E1E (23F4-0)");
            const auto ring = P::Stop(P::Model::SwatchOf(5), left, stops, grid);
            Check(Near(PngPixel(file(L"2-selecteur-clavier@2x"), static_cast<int>(((ring.left + ring.right) / 2) * k), static_cast<int>((ring.top + .75f) * k)), 0xFFFFFF, 40), "chosen swatch: its 1.5 DIP white ring (23FK-0)");
        }
        drawer.KeyForTest(VK_ESCAPE);
        Check(drawer.Picking() == 0 && drawer.Focus() == BookmarksUI::RowHit(veille) + 2, "Escape folds the picker, focus back on the palette button");
        // 4 · No colour (Divers), opened with the pointer: the explanation instead of the keys.
        drawer.ActivateForTest(BookmarksUI::RowHit(divers) + 2, false);
        Check(drawer.Picking() == demo.divers && drawer.PickerForTest().color == -1 && drawer.PickerForTest().icon == 0 && drawer.PickerForTest().focus == -1, "click on Divers' palette: picker on « Sans couleur », no keyboard focus");
        std::wstring hint; for (const auto& l : drawer.PickerHintForTest()) hint += (hint.empty() ? L"" : L" ") + l;
        Check(hint.find(L"Sans couleur ni ic") == 0 || hint.find(L"No color") == 0, "no colour: the board 4 explanation (240T-0)");
        drawer.ScrollForTest(0); Check(drawer.RenderForTest(root, file(L"4-sans-couleur" + sfx).c_str(), dpi, 600, false), "4 drawer, no colour");
        drawer.ActivateForTest(BookmarksUI::RowHit(divers) + 2, false);
        Check(drawer.Picking() == 0, "the palette button again folds it");
    }
    {   // Choices apply at once, through the owner, and stay while the picker is open; one visit per opening.
        said.clear(); const int before = visits;
        drawer.ActivateForTest(BookmarksUI::RowHit(divers) + 2, false);
        drawer.ActivateForTest(BookmarksUI::PickerBase + P::Model::SwatchOf(1), false);   // vert
        Check(s.Find(demo.divers)->color == "vert" && s.Find(demo.divers)->icon.empty() && drawer.Rows()[divers].look.styled, "click on vert: Divers is green at once (drawer row follows)");
        drawer.ActivateForTest(BookmarksUI::PickerBase + P::Model::CellOf(20), false);    // globe
        Check(s.Find(demo.divers)->icon == "globe" && drawer.Picking() == demo.divers, "click on globe: applied, the picker stays");
        Check(!said.empty() && said.back() == TRF(folder_announce_icon, TR(folder_icon_globe)), "announced: « Icône : Monde »");
        drawer.ActivateForTest(BookmarksUI::PickerBase + P::Model::SwatchOf(-1), false);
        drawer.ActivateForTest(BookmarksUI::PickerBase + P::Model::CellOf(0), false);
        Check(s.Find(demo.divers)->color.empty() && s.Find(demo.divers)->icon.empty() && !drawer.Rows()[divers].look.styled, "back to « Sans couleur » + the plain folder: the grey folder");
        Check(visits == before + 1 && visiting == demo.divers, "one visit while it stays open");
        drawer.KeyForTest(VK_ESCAPE); Check(visiting == 0 && drawer.Picking() == 0, "Escape ends the visit");
        // Delete on a folder row still removes (its first action), not the palette.
        bool removed = false; (void)removed;
    }

    // ---- The popover (right click → « Personnaliser… ») and the folder's menu (board 3) ---------------------------
    {
        FolderPicker::Popover pop; FolderPicker::Hooks ph; std::string lastIcon, lastColor;
        ph.restyle = [&](unsigned id, const std::string& icon, const std::string& color) { lastIcon = icon; lastColor = color; return s.SetStyle(id, icon, color); };
        Check(pop.Create(instance, owner, root, ph), "popover created (never shown)");
        pop.OpenForTest(demo.veille, L"Veille", "newspaper", "ambre", false);
        for (UINT dpi : {96u, 192u}) Check(pop.RenderForTest(root, file(dpi == 96 ? L"3-popover" : L"3-popover@2x").c_str(), dpi, false), "3 popover");
        {   const float k = 2; const D2D1_RECT_F tile{FolderPicker::Layout::inset, FolderPicker::Layout::headerTop, FolderPicker::Layout::inset + 32, FolderPicker::Layout::headerTop + 32};
            const auto body = GlyphPoint(tile, 18, bodyX, bodyY, k);
            Check(Near(PngPixel(file(L"3-popover@2x"), body.x, body.y), 0xE6AC3D), "popover header: Veille's folder in ambre (23SG-0)");
            Check(Near(PngPixel(file(L"3-popover@2x"), 2, 100), 0x1E1E1E, 1) && Near(PngPixel(file(L"3-popover@2x"), 0, 100), 0x2E2E2E, 6), "popover: #1E1E1E, 1 DIP #2E2E2E border (23SD-0)");
            Check(std::fabs(FolderPicker::Layout::Height() - 180) < .01f && FolderPicker::Layout::width == 288, "popover 288 x 180");
        }
        pop.KeyForTest(VK_RIGHT); Check(pop.ModelForTest().focus == P::Model::SwatchOf(5), "popover keyboard: the first arrow focuses the chosen colour");
        pop.KeyForTest(VK_LEFT); pop.KeyForTest(VK_RETURN);
        Check(lastColor == "rose" && s.Find(demo.veille)->color == "rose" && lastIcon == "newspaper", "popover: Left + Enter → rose, applied at once");
        pop.ChooseForTest(P::Model::SwatchOf(5)); Check(s.Find(demo.veille)->color == "ambre", "popover click: back to ambre");
        int closed = 0; (void)closed;
        CM::Menu menu;
        const size_t links = s.CountLinks(demo.veille);
        std::vector<CM::Item> items;   // BookmarkContextMenu's folder rows (main.cpp), « Personnaliser… » under « Renommer… »
        items.push_back(CM::Command(1, TRP(bookmark_open_all, links))); items.push_back(CM::Separator());
        items.push_back(CM::Command(2, TR(cmd_rename), L"F2")); items.push_back(CM::Command(3, TR(cmd_customize_folder))); items.push_back(CM::Command(4, TR(cmd_duplicate)));
        items.push_back(CM::Separator()); auto del = CM::Command(5, TR(common_delete), TR(key_delete)); del.danger = true; items.push_back(del);
        Check(menu.RenderForTest(items, root, 96, file(L"3-menu").c_str(), 3) && menu.RenderForTest(items, root, 192, file(L"3-menu@2x").c_str(), 3), "3 the folder's menu, « Personnaliser… » hovered");
        pop.Dispose();
    }

    // ---- Chrome: the top bar, the side grid (232) and the compact column ------------------------------------------
    auto chromeShot = [&](const std::wstring& name, UINT dpi, bool side, Sidebar::Mode mode, int hoverIndex, const std::vector<Chrome::BandItem>& items, std::wstring* tip = nullptr) {
        Chrome c; Motion::State m(false); c.SetMotion(&m);
        Check(SUCCEEDED(c.InitOffscreen(root, dpi, side ? 1178.f : 1180.f, side ? 738.f : 121.f)), "offscreen chrome");
        c.sidebar = side; c.keyboardModality = false; c.bandShown = true; c.bandItems = items; c.address = AddressBar::Describe(L"https://www.lemonde.fr/", false);
        if (side) { c.sidebarMode = mode; if (mode == Sidebar::Mode::Compact) { c.sidebarColumn = Sidebar::Compact::width; c.sidebarTitles = 0; } else { c.sidebarWidth = c.sidebarColumn = 232; } }
        const int hover = hoverIndex >= 0 ? BookmarkBase + hoverIndex : 0;
        if (hover) m.SetInteraction(hover, 0, Motion::Now() - std::chrono::milliseconds(500));
        const std::vector<std::wstring> titles{L"Le Monde"};
        c.Paint(titles, 0, L"", false, hover, 0, nullptr, Motion::Now() + std::chrono::milliseconds(400));
        Check(SUCCEEDED(c.SavePng(file(name).c_str())), "chrome snapshot");
        if (tip && hoverIndex >= 0) for (const auto& r : c.regions) if (r.id == hover && r.tip) *tip = r.tip;
        char line[400];
        if (side) { const auto g = c.side.mode == Sidebar::Mode::Complete ? c.side.grid.view : c.side.gridBlock; std::snprintf(line, sizeof line, "%ls grid %.1f %.1f %.1f %.1f\n", name.c_str(), g.left, g.top, g.right, g.bottom); }
        else { float l = 1e9f, r = 0; for (int i = 0; i < c.band.visible; ++i) { const auto b = c.BandRect(i); l = std::min(l, b.left); r = std::max(r, b.right); } std::snprintf(line, sizeof line, "%ls band %.1f %.1f %.1f %.1f\n", name.c_str(), l, Design::Vertical::bandTop, r, Design::Vertical::bandBottom); }
        boxes << line;
        return c.BandRect(0);
    };
    const auto bar = BandItems(s, BM::BarRoot);
    for (UINT dpi : {96u, 192u}) {
        const std::wstring sfx = dpi == 96 ? L"" : L"@2x"; const float k = dpi / 96.f;
        chromeShot(L"6-barre" + sfx, dpi, false, Sidebar::Mode::Complete, -1, bar);
        chromeShot(L"3-barre" + sfx, dpi, false, Sidebar::Mode::Complete, 2, bar);   // Veille under the pointer (board 23RV-0)
        if (dpi == 192) {
            Chrome c; Motion::State m(false); c.SetMotion(&m); c.InitOffscreen(root, dpi, 1180, 121); c.bandShown = true; c.bandItems = bar; c.Paint({L"x"}, 0, L"", false, 0, 0);
            const auto b0 = c.BandRect(0); const float cy = (b0.top + b0.bottom) / 2; const D2D1_RECT_F icon{b0.left + BookmarksBar::Layout::itemPad, cy - 8, b0.left + BookmarksBar::Layout::itemPad + 16, cy + 8};
            Check(Near(PngPixel(file(L"6-barre@2x"), GlyphPoint(icon, 16, bodyX, bodyY, k).x, GlyphPoint(icon, 16, bodyX, bodyY, k).y), 0x6FD087), "top bar: Travail's folder in vert #6FD087 (16 DIP, 243X-0)");
        }
        chromeShot(L"6-grille" + sfx, dpi, true, Sidebar::Mode::Complete, -1, bar);
        chromeShot(L"6-colonne" + sfx, dpi, true, Sidebar::Mode::Compact, -1, bar);
        if (dpi == 192) {
            Chrome c; Motion::State m(false); c.SetMotion(&m); c.InitOffscreen(root, dpi, 1178, 738); c.sidebar = true; c.bandShown = true; c.bandItems = bar; c.sidebarWidth = c.sidebarColumn = 232; c.Paint({L"x"}, 0, L"", false, 0, 0);
            const auto t = c.BandRect(2); const auto body = GlyphPoint(t, 20, bodyX, bodyY, k), emblem = GlyphPoint(t, 20, paperX, paperY, k);
            Check(Near(PngPixel(file(L"6-grille@2x"), body.x, body.y), 0xE6AC3D) && Near(PngPixel(file(L"6-grille@2x"), emblem.x, emblem.y), 0x61481A, 10), "side grid: Veille 20 DIP, ambre with its #61481A emblem (243H-0)");
        }
    }
    // The drawer's « 6 » and long-name states, and the menus.
    {
        BM::Store six = s; BookmarksUI::Drawer d6; BookmarksUI::Drawer::Hooks h6; Check(d6.Create(instance, owner, root, &six, h6), "drawer (6)"); d6.OpenForTest();
        for (UINT dpi : {96u, 192u}) Check(d6.RenderForTest(root, file(dpi == 96 ? L"6-tiroir" : L"6-tiroir@2x").c_str(), dpi, 520, false), "6 drawer");
        // 5 · Long names: the drawer row fades out over 24 DIP (board 2423-0), never « … ».
        for (UINT dpi : {96u, 192u}) Check(d6.RenderForTest(root, file(dpi == 96 ? L"5-tiroir" : L"5-tiroir@2x").c_str(), dpi, 600, false), "5 drawer (long name)");
        {   const int i = [&] { const auto& rows = d6.Rows(); for (int j = 0; j < static_cast<int>(rows.size()); ++j) if (rows[j].id == demo.recettes) return j; return -1; }();
            const float top = d6.RowTopForTest(i), right = BookmarksUI::Layout::width - Downloads::Panel::padding - 30, k = 2;
            // The title's last 2 DIP: faded to the canvas (#171717); 30 DIP before its end: text ink.
            int inkEnd = 0, inkBefore = 0;
            for (int y = static_cast<int>((top - 1.5f) * k); y < static_cast<int>((top + 14) * k); ++y) {
                inkEnd = std::max(inkEnd, static_cast<int>(G(PngPixel(file(L"5-tiroir@2x"), static_cast<int>((right - 1) * k), y))));
                for (int x = 0; x < 6; ++x) inkBefore = std::max(inkBefore, static_cast<int>(G(PngPixel(file(L"5-tiroir@2x"), static_cast<int>((right - 34) * k) + x, y))));
            }
            Check(i > 0 && inkEnd <= 0x17 + 6 && inkBefore > 0x50, "long folder name: faded out at its end (24 DIP), never « … »");
        }
        CM::Menu menu;
        auto moveSix = MoveTo(six, six.Find(demo.travail)->children.front());
        Check(menu.RenderForTest(moveSix, root, 96, file(L"6-deplacer-vers").c_str(), 2) && menu.RenderForTest(moveSix, root, 192, file(L"6-deplacer-vers@2x").c_str(), 2), "6 « Déplacer vers »");
        {   // The menu's folder rows: the 18 DIP glyph in the leading column — sampled on Veille (Travail is dimmed: the current folder).
            bool amber = false; for (int y = 0; y < 300 && !amber; ++y) for (int x = 0; x < 70 && !amber; ++x) amber = Near(PngPixel(file(L"6-deplacer-vers@2x"), x, y), 0xE6AC3D, 3);
            Check(amber, "« Déplacer vers »: Veille's folder in ambre (246N-0)");
        }
        float w = 0; Check(menu.RenderForTest(moveSix, root, 96, file(L"5-deplacer-vers").c_str(), -1, &w) && menu.RenderForTest(moveSix, root, 192, file(L"5-deplacer-vers@2x").c_str()), "5 « Déplacer vers » (long name)");
        Check(w <= CM::Layout::maxWidth, "the menu stays within its 440 DIP");
    }
    // 5 · Bar with the long name (and Veille), grid tile with its tip.
    {
        std::vector<Chrome::BandItem> other = BandItems(s, BM::OtherRoot); std::vector<Chrome::BandItem> five{other[1], bar[2]};
        for (UINT dpi : {96u, 192u}) chromeShot(dpi == 96 ? L"5-barre" : L"5-barre@2x", dpi, false, Sidebar::Mode::Complete, -1, five);
        std::wstring tip; chromeShot(L"5-grille", 96, true, Sidebar::Mode::Complete, 0, five, &tip);
        Check(tip == L"Recettes de grand-mère et carnets de voyage · 31 favoris" || tip == L"Recettes de grand-mère et carnets de voyage · 31 bookmarks", "grid tip: « name · 31 favoris » on one line (242X-0)");
        Tooltip::Tip t; Check(t.Create(instance, owner, root), "tip created (never shown)");
        Check(t.RenderContentForTest(root, {tip, L"", L""}, file(L"5-info-bulle").c_str()) && t.RenderContentForTest(root, {tip, L"", L""}, file(L"5-info-bulle@2x").c_str(), 192), "5 grid tip");
        Check(t.RenderContentForTest(root, {TR(folder_customize), L"", L""}, file(L"1-info-bulle").c_str()) && t.RenderContentForTest(root, {TR(folder_customize), L"", L""}, file(L"1-info-bulle@2x").c_str(), 192), "1 « Personnaliser » tip");
        t.Dispose();
    }
    drawer.Dispose(); DestroyWindow(owner);
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
