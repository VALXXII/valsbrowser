// #138 A favorites folder's look (src/bookmark-folder-style.h): palette and icons from the Paper board « Dossiers de
// favoris · C finale (validée) », how a stored look resolves, the picker's geometry and its keyboard model.
#include "../src/bookmark-folder-style.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

using namespace FolderStyle;

int main() {
    // ---- Palette: the profiles' six colours and their darker emblem (board table, colour x 0.42) ----------------
    {
        CHECK(colorCount == 6);
        const std::uint32_t folders[] = {0xB3B3B3, 0x6FD087, 0x4FC3D9, 0xB49BFF, 0xF08BC4, 0xE6AC3D};
        const std::uint32_t emblems[] = {0x4B4B4B, 0x2F5739, 0x21525B, 0x4C416B, 0x653A52, 0x61481A};
        const char* keys[] = {"gris", "vert", "cyan", "violet", "rose", "ambre"};
        for (int i = 0; i < colorCount; ++i) {
            CHECK(colors[i].folder == folders[i] && colors[i].emblem == emblems[i] && !std::strcmp(colors[i].key, keys[i]));
            for (int shift : {16, 8, 0}) {   // each channel of the emblem is the folder's x 0.42, rounded, as the board says
                const int f = (folders[i] >> shift) & 0xFF, e = (emblems[i] >> shift) & 0xFF;
                CHECK(e == static_cast<int>(f * 0.42 + 0.5));
            }
            CHECK(ColorIndex(keys[i]) == i);
        }
        CHECK(noneFolder == 0xB9B9B9 && noneEmblem == 0x4E4E4E && ColorIndex("") == -1 && ColorIndex("turquoise") == -1);
    }
    // ---- 24 Phosphor Fill icons in the picker's order; the first is the plain folder --------------------------
    {
        CHECK(iconCount == 24 && Picker::cells == 24 && Picker::rows == 3);
        const char* order[] = {"folder", "briefcase", "house", "star", "heart", "code", "book-open", "music-notes", "shopping-cart", "airplane",
                               "game-controller", "graduation-cap", "camera", "palette", "film-strip", "newspaper", "chat-circle", "coffee",
                               "lightbulb", "rocket", "globe", "leaf", "paw-print", "lightning"};
        std::set<std::string> paths;
        for (int i = 0; i < iconCount; ++i) {
            CHECK(!std::strcmp(icons[i].key, order[i]));
            if (i == 0) { CHECK(icons[i].emblem == nullptr); continue; }
            CHECK(icons[i].emblem && icons[i].emblem[0] == 'M' && paths.insert(icons[i].emblem).second);   // distinct path data
            CHECK(IconIndex(order[i]) == i);
        }
        CHECK(IconIndex("") == 0 && IconIndex("folder") == 0 && IconIndex("unicorn") == 0);
        CHECK(folderPath && !std::strncmp(folderPath, "M216,72H131.31L104,44.69", 24) && PalettePath() == icons[13].emblem);
        CHECK(!std::strcmp(ColorKey(5), "ambre") && !std::strcmp(ColorKey(-1), "") && !std::strcmp(IconKey(15), "newspaper") && !std::strcmp(IconKey(0), ""));
    }
    // ---- Resolve: board states 4 (no colour) and 6 ----------------------------------------------------------
    {
        CHECK(!Resolve("", "").styled && !Resolve("folder", "").styled && !Resolve("unicorn", "turquoise").styled);   // the grey folder of today
        const Look amber = Resolve("newspaper", "ambre");
        CHECK(amber.styled && amber.folder == 0xE6AC3D && amber.emblem == 0x61481A && amber.emblemPath == icons[15].emblem);
        const Look greyIcon = Resolve("book-open", "");    // « Une icône sans couleur reste grise » (240X-0)
        CHECK(greyIcon.styled && greyIcon.folder == 0xB9B9B9 && greyIcon.emblem == 0x4E4E4E && greyIcon.emblemPath == icons[6].emblem);
        const Look colourOnly = Resolve("", "vert");
        CHECK(colourOnly.styled && colourOnly.folder == 0x6FD087 && !colourOnly.emblemPath);
        CHECK(Size::For(Size::tile, Size::tilePlain, amber) == 20 && Size::For(Size::tile, Size::tilePlain, colourOnly) == 16);
        CHECK(Size::bar == 16 && Size::drawer == 18 && Size::menu == 18 && Size::cell == 18 && nameFade == 24);
    }
    // ---- Geometry (board 23F4-0 / 23SD-0) ----------------------------------------------------------------------
    {
        CHECK(Picker::gridWidth == 236 && Picker::gridHeight == 86 && Picker::swatchesWidth == 7 * 24 + 6 * 8);
        CHECK(Picker::PopoverHeight() == 1 + 10 + 32 + 8 + 24 + 8 + 86 + 10 + 1 && Picker::DrawerHeight(32) == 10 + 24 + 10 + 86 + 10 + 32 + 10);
        const auto s0 = Picker::Stop(0, 100, 50, 84), s6 = Picker::Stop(6, 100, 50, 84);
        CHECK(s0.left == 100 && s0.right == 124 && s0.top == 50 && s6.left == 100 + 6 * 32 && s6.bottom == 74);
        const auto c0 = Picker::Stop(Picker::swatches, 100, 50, 84), c9 = Picker::Stop(Picker::swatches + 9, 100, 50, 84), last = Picker::Stop(Picker::count - 1, 100, 50, 84);
        CHECK(c0.left == 100 && c0.top == 84 && c0.right == 126 && c9.left == 130 && c9.top == 114 && last.left == 100 + 7 * 30 && last.bottom == 84 + 86);
        CHECK(Picker::HitStop(101, 51, 100, 50, 84) == 0 && Picker::HitStop(131, 115, 100, 50, 84) == Picker::swatches + 9);
        CHECK(Picker::HitStop(126.5f, 90, 100, 50, 84) == -1 && Picker::HitStop(99, 51, 100, 50, 84) == -1);   // the gaps between stops
    }
    // ---- Keyboard model (board 2) -----------------------------------------------------------------------------
    {
        using Picker::Key; using Picker::Outcome;
        Picker::Model m; m.color = 5; m.icon = 15;   // Veille: ambre + newspaper (board 2)
        CHECK(m.Press(Key::Right) == Outcome::Moved && m.focus == Picker::Model::SwatchOf(5));   // the first key focuses the chosen colour
        CHECK(m.focus == 6 && m.Press(Key::Right) == Outcome::None && m.focus == 6);   // ambre is the last swatch: clamped
        CHECK(m.Press(Key::Home) == Outcome::Moved && m.focus == 0 && m.Press(Key::Left) == Outcome::None);
        CHECK(m.Press(Key::End) == Outcome::Moved && m.focus == 6);
        CHECK(m.Press(Key::Tab) == Outcome::Moved && m.focus == Picker::Model::CellOf(15));         // Tab: the grid, on the chosen icon
        CHECK(m.Press(Key::BackTab) == Outcome::Moved && m.focus == Picker::Model::SwatchOf(5));     // back: the chosen colour
        CHECK(m.Press(Key::Tab) == Outcome::Moved);
        CHECK(m.Press(Key::Down) == Outcome::Moved && m.focus == Picker::Model::CellOf(23) && m.Press(Key::Down) == Outcome::None);
        CHECK(m.Press(Key::Left) == Outcome::Moved && m.focus == Picker::Model::CellOf(22));
        CHECK(m.Press(Key::Up) == Outcome::Moved && m.focus == Picker::Model::CellOf(14) && m.Press(Key::Up) == Outcome::Moved && m.focus == Picker::Model::CellOf(6));
        CHECK(m.Press(Key::Up) == Outcome::Moved && m.focus == 6);                                    // row 0 → the swatch above (column 6)
        CHECK(m.Press(Key::Left) == Outcome::Moved && m.Press(Key::Down) == Outcome::Moved && m.focus == Picker::Model::CellOf(5));   // swatch 5 → cell 5
        m.focus = Picker::Model::CellOf(7); CHECK(m.Press(Key::Up) == Outcome::Moved && m.focus == 6); // column 7 has no swatch: the last one
        m.focus = Picker::Model::CellOf(8); CHECK(m.Press(Key::Left) == Outcome::Moved && m.focus == Picker::Model::CellOf(7));   // reading order across rows
        // Enter / Space chooses and applies at once; the same choice again changes nothing.
        m.focus = Picker::Model::CellOf(19); CHECK(m.Press(Key::Choose) == Outcome::Chosen && m.icon == 19 && m.color == 5);
        CHECK(m.Press(Key::Choose) == Outcome::Unchanged && m.icon == 19);
        m.focus = 0; CHECK(m.Press(Key::Choose) == Outcome::Chosen && m.color == -1 && m.icon == 19);   // « Sans couleur »: the icon stays (grey)
        CHECK(m.Choose(Picker::Model::CellOf(0)) == Outcome::Chosen && m.icon == 0 && m.focus == Picker::swatches);   // the plain folder
        CHECK(m.Choose(-1) == Outcome::None && m.Choose(Picker::count) == Outcome::None);
        CHECK(m.Press(Key::Close) == Outcome::Close);
        Picker::Model fresh; CHECK(fresh.Press(Key::Choose) == Outcome::Moved && fresh.focus == 0 && fresh.color == -1);   // a first Enter only focuses
    }
    std::puts("folder style tests passed");
    return 0;
}
