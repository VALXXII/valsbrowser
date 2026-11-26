// A tab dropped on the favorites bar (src/tab-favorite-drop.h): where it would be added, duplicates, folders, the gaps,
// a full bar, and the bar laid out with the new item's place opened (board 08's hollow slot, the others stepping aside).
#include "../src/tab-favorite-drop.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
using namespace TabFavorite;
namespace BM = Bookmarks;
static int checks = 0;
#define OK(x) do { CHECK(x); ++checks; } while (0)

int main() {
    BM::Store s;
    s.Add(BM::BarRoot, BM::End, L"A", L"https://a.example/", 1);
    const BM::Id f = s.AddFolder(BM::BarRoot, BM::End, L"Dossier", 1);
    s.Add(BM::BarRoot, BM::End, L"B", L"https://b.example/", 1);
    s.Add(f, BM::End, L"In", L"https://in.example/", 1);
    const std::vector<BookmarkDrag::Box> boxes{{6, 86}, {88, 188}, {190, 270}};   // A, folder, B (2 DIP gaps)
    const std::wstring page = L"https://new.example/";

    // ---- Where it lands ---------------------------------------------------------------------------------------
    auto t = BarTarget(s, page, boxes, 20);   OK(t.valid && t.parent == BM::BarRoot && t.index == 0 && t.slot == 0 && t.into < 0 && t.duplicate == -1);
    t = BarTarget(s, page, boxes, 60);        OK(t.valid && t.slot == 1 && t.index == 1);                     // right half of A: after it
    t = BarTarget(s, page, boxes, 87);        OK(t.valid && t.slot == 1);                                     // the 2 DIP gap A|folder: between them
    t = BarTarget(s, page, boxes, 138);       OK(t.valid && t.into == 1 && t.parent == f && t.index == BM::End && t.slot < 0);   // middle half of a folder: into it
    t = BarTarget(s, page, boxes, 95);        OK(t.valid && t.into < 0 && t.slot == 1);                      // a folder's edge: before it
    t = BarTarget(s, page, boxes, 180);       OK(t.valid && t.into < 0 && t.slot == 2);                      // its right edge: after it
    t = BarTarget(s, page, boxes, 600);       OK(t.valid && t.slot == 3 && t.index == 3);                    // past the last: at the end
    t = BarTarget(s, page, boxes, 2);         OK(t.valid && t.slot == 0);                                     // left of the first
    { BM::Store none; t = BarTarget(none, page, {}, 40); OK(t.valid && t.slot == 0 && t.parent == BM::BarRoot); }   // empty bar
    OK(!BarTarget(s, L"about:blank", boxes, 20).valid);                                                       // not a page a favorite can hold
    OK(!BarTarget(s, L"", boxes, 20).valid);

    // ---- Duplicates: the same page already directly in the destination adds nothing --------------------------------
    t = BarTarget(s, L"https://b.example/", boxes, 20);    OK(!t.valid && t.slot < 0 && t.duplicate == 2);      // B is on the bar: points at B
    t = BarTarget(s, L"https://b.example", boxes, 20);     OK(!t.valid && t.duplicate == 2);                    // same page (UrlKey)
    t = BarTarget(s, L"https://in.example/", boxes, 138);  OK(!t.valid && t.into < 0 && t.duplicate == 1);      // already in that folder
    t = BarTarget(s, L"https://in.example/", boxes, 20);   OK(t.valid && t.slot == 0);                          // in a folder only: the bar may hold it too
    { BM::Store x = s; x.Add(BM::BarRoot, BM::End, L"Hidden", L"https://hidden.example/", 1);                    // behind « » »
      t = BarTarget(x, L"https://hidden.example/", boxes, 20); OK(!t.valid && t.duplicate == -2); }

    // ---- The bar with the place opened (BookmarksBar::Arrange with the new item) ------------------------------------
    const std::vector<float> naturals{60, 80, 60};
    auto p = Opened(1180, naturals, 1, 100, false);
    OK(p.slotShown && p.slotLeft == 6 + 60 + 2 && p.slotWidth == 100);                                        // after A
    OK(p.left[0] == 6 && p.left[1] == 6 + 60 + 2 + 100 + 2 && p.left[2] == p.left[1] + 80 + 2);              // the others step aside by its width + gap
    OK(p.shown[0] && p.shown[1] && p.shown[2] && !p.overflow);
    p = Opened(1180, naturals, 0, 100, false);   OK(p.slotLeft == 6 && p.left[0] == 6 + 100 + 2);
    p = Opened(1180, naturals, 3, 100, false);   OK(p.slotLeft == p.left[2] + 60 + 2 && p.left[0] == 6);      // at the end: nobody moves
    p = Opened(1180, naturals, 99, 100, false);  OK(p.slotShown && p.slotLeft == p.left[2] + 60 + 2);         // clamped
    { // A full bar: the new item at the end goes behind « » » (the bar shows where it really goes), nobody is pushed off.
      std::vector<float> full(12, 160.f);
      const auto rest = BookmarksBar::Arrange(1180, std::vector<BookmarksBar::Item>(12, BookmarksBar::Item{160}));
      p = Opened(1180, full, rest.visible, 160, false);
      OK(p.overflow && !p.slotShown);
      for (int i = 0; i < rest.visible; ++i) OK(p.shown[static_cast<std::size_t>(i)]);
      // At the start of a full bar: the place opens, the last shown item goes behind « » ».
      p = Opened(1180, full, 0, 160, false);
      OK(p.slotShown && p.slotLeft == 6 && p.overflow && !p.shown[static_cast<std::size_t>(rest.visible - 1)]); }
    { // Narrower titles when crowded: Arrange's shrinking applies to the opened bar too (never wider than at rest).
      std::vector<float> many(7, 160.f);
      p = Opened(1180, many, 3, 160, false);
      float sum = 0; for (std::size_t i = 0; i < many.size(); ++i) if (p.shown[i]) sum += p.width[i] + 2;
      OK(p.slotShown && sum + p.slotWidth <= 1180 - 12 + .5f); }

    // ---- The column's grid (side panel: rows of tiles; compact: one column) ----------------------------------------
    { // Side panel: 5 tiles in one row of 232 (A, folder, B, C, D), the view the row.
      BM::Store g; g.Add(BM::BarRoot, BM::End, L"A", L"https://a.example/", 1); const BM::Id gf = g.AddFolder(BM::BarRoot, BM::End, L"F", 1);
      for (const wchar_t* u : {L"https://b.example/", L"https://c.example/", L"https://d.example/"}) g.Add(BM::BarRoot, BM::End, L"x", u, 1);
      const auto grid = Sidebar::MakeGrid(8, 100, 232, 5, 2, 0);
      const auto& t = grid.tiles;
      auto tt = GridTarget(g, page, t, grid.view, false, t[0].left + 2, 120);          OK(tt.valid && tt.slot == 0);
      tt = GridTarget(g, page, t, grid.view, false, t[0].right - 2, 120);              OK(tt.valid && tt.slot == 1);
      tt = GridTarget(g, page, t, grid.view, false, (t[1].left + t[1].right) / 2, 120); OK(tt.valid && tt.into == 1 && tt.parent == gf);   // a folder's middle
      tt = GridTarget(g, page, t, grid.view, false, t[1].right + 1, 120);              OK(tt.valid && tt.slot == 2);                     // the gap: between them
      tt = GridTarget(g, page, t, grid.view, false, t[4].right - 2, 120);              OK(tt.valid && tt.slot == 5);
      tt = GridTarget(g, page, t, grid.view, false, 100, grid.view.bottom + 10);       OK(tt.valid && tt.slot == 5);                     // under the grid: after the last
      tt = GridTarget(g, L"https://c.example/", t, grid.view, false, t[0].left + 2, 120); OK(!tt.valid && tt.duplicate == 3);
      // Opened: one more tile in a full row of 5 -> a second row; the place at the end falls outside the view.
      auto o = GridOpened(grid, 2);
      OK(o.slotShown && o.tiles[0].left == t[0].left && o.tiles[2].left == t[3].left && o.tiles[4].top > grid.view.bottom - 1);      // the last one leaves the row
      o = GridOpened(grid, 5);  OK(!o.slotShown && o.tiles[4].left == t[4].left);                                                       // end of a full row: no room
      // 3 tiles share a row: the place opens and the row re-divides (4 tiles in 232).
      const auto three = Sidebar::MakeGrid(8, 100, 232, 3, 2, 0);
      o = GridOpened(three, 3); OK(o.slotShown && std::fabs((o.slot.right - o.slot.left) - (232 - 3 * 6) / 4.f) < .01f && o.tiles[0].right < three.tiles[0].right);
      // 7 tiles (a second row with room): the end place shows in it.
      const auto seven = Sidebar::MakeGrid(8, 100, 232, 7, 2, 0);
      o = GridOpened(seven, 7); OK(o.slotShown && o.slot.top == seven.tiles[5].top && o.slot.left == seven.tiles[2].left);
      // Scrolled (unfolded grid): the same scroll.
      const auto scrolled = Sidebar::MakeGrid(8, 100, 232, 12, 1, 20);
      o = GridOpened(scrolled, 0); OK(o.slot.top == scrolled.tiles[0].top && o.tiles[0].left == scrolled.tiles[1].left); }
    { // Compact: 3 tiles 40 x 36, gap 4.
      BM::Store g; for (const wchar_t* u : {L"https://a.example/", L"https://b.example/", L"https://c.example/"}) g.Add(BM::BarRoot, BM::End, L"x", u, 1);
      const std::vector<Sidebar::Box> t{{0, 100, 40, 136}, {0, 140, 40, 176}, {0, 180, 40, 216}}; const Sidebar::Box view{0, 100, 40, 216};
      auto tt = GridTarget(g, page, t, view, true, 20, 104);  OK(tt.valid && tt.slot == 0);      // upper half: before
      tt = GridTarget(g, page, t, view, true, 20, 130);       OK(tt.valid && tt.slot == 1);      // lower half: after
      tt = GridTarget(g, page, t, view, true, 20, 138);       OK(tt.valid && tt.slot == 1);      // the 4 DIP gap
      tt = GridTarget(g, page, t, view, true, 20, 214);       OK(tt.valid && tt.slot == 3);
      auto o = ColumnOpened(t, 3, 1); OK(o.slotShown && o.slot.top == 140 && o.tiles[0].top == 100 && o.tiles[1].top == 180 && o.tiles[2].top == 220);
      o = ColumnOpened(t, 3, 3); OK(!o.slotShown && o.slot.top == 220); }

    std::printf("PASS: tab favorite drop rules (%d checks)\n", checks);
    return 0;
}
