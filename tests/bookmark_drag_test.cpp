// Bookmark drag and drop rules (src/bookmark-drag.h): bar targets, drawer targets, commit-on-drop only.
#include "../src/bookmark-drag.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
using namespace BookmarkDrag;
namespace BM = Bookmarks;
static int checks = 0;
#define OK(x) do { CHECK(x); ++checks; } while (0)

int main() {
    BM::Store s;
    const BM::Id a = s.Add(BM::BarRoot, BM::End, L"A", L"https://a.example/", 1);
    const BM::Id f = s.AddFolder(BM::BarRoot, BM::End, L"Dossier 日本", 1);
    const BM::Id b = s.Add(BM::BarRoot, BM::End, L"B", L"https://b.example/", 1);
    const BM::Id inner = s.AddFolder(f, BM::End, L"Inner", 1);
    const BM::Id c = s.Add(inner, BM::End, L"C", L"https://c.example/", 1);   // the screenshot case: a link deep in a folder
    const BM::Id o = s.Add(BM::OtherRoot, BM::End, L"O", L"https://o.example/", 1);
    const std::vector<Box> boxes{{12, 92}, {98, 198}, {204, 284}};   // A, folder, B

    // ---- Bar targets --------------------------------------------------------------------------------------------
    auto t = BarTarget(s, c, boxes, 20);    OK(t.drop.valid && t.drop.parent == BM::BarRoot && t.drop.index == 0 && t.slot == 0 && t.marker == 9);
    t = BarTarget(s, c, boxes, 80);         OK(t.drop.index == 1 && t.slot == 1 && t.marker == 95);
    t = BarTarget(s, c, boxes, 150);        OK(t.into == 1 && t.drop.parent == f && t.drop.index == BM::End);   // middle of a folder: into
    t = BarTarget(s, c, boxes, 105);        OK(t.into < 0 && t.drop.index == 1);                                   // its edge: before it
    t = BarTarget(s, c, boxes, 400);        OK(t.drop.index == 3 && t.marker == 287);                             // past the last: append
    t = BarTarget(s, c, boxes, 2);          OK(t.drop.index == 0);
    t = BarTarget(s, f, boxes, 150);        OK(t.into < 0);                                                         // a folder over itself: never into itself
    { BM::Store x = s; OK(Commit(x, f, t.drop) == Result::Unchanged && x == s); }
    t = BarTarget(s, inner, boxes, 150);    OK(t.into == 1);                                                        // into its own parent: allowed (no-op move)
    BM::Store none; const BM::Id lone = none.Add(BM::OtherRoot, BM::End, L"L", L"https://l.example/", 1);
    t = BarTarget(none, lone, {}, 40);      OK(t.drop.valid && t.drop.index == 0);                                 // empty bar
    OK(!BarTarget(s, 9999, boxes, 20).drop.valid);                                                                  // item gone (another window)

    // ---- Commit: only on drop; same-folder index adjustment; refusals change nothing ----------------------------
    { BM::Store x = s; OK(Commit(x, c, BarTarget(x, c, boxes, 80).drop) == Result::Moved && x.Find(c)->parent == BM::BarRoot && x.IndexOf(c) == 1 && x.Find(inner)->children.empty()); }
    { BM::Store x = s; OK(Commit(x, a, BarTarget(x, a, boxes, 270).drop) == Result::Moved && x.IndexOf(a) == 2); }        // A after B: final index 2
    { BM::Store x = s; OK(Commit(x, a, BarTarget(x, a, boxes, 20).drop) == Result::Unchanged && x == s); }                 // dropped where it was
    { BM::Store x = s; OK(Commit(x, b, {BM::BarRoot, BM::End, true}) == Result::Unchanged && x == s); }
    { BM::Store x = s; OK(Commit(x, f, {inner, BM::End, true}) == Result::Refused && x == s); }                            // cycle
    { BM::Store x = s; OK(Commit(x, BM::BarRoot, {BM::OtherRoot, BM::End, true}) == Result::Refused && x == s); }          // roots never move
    {   // depth: a 7-level folder cannot go into a folder at depth 3
        BM::Store x; BM::Id deep = x.AddFolder(BM::OtherRoot, BM::End, L"d1", 1), p = deep;
        for (int i = 2; i <= 6; ++i) p = x.AddFolder(p, BM::End, L"d" + std::to_wstring(i), 1);
        const BM::Id host = x.AddFolder(BM::BarRoot, BM::End, L"h", 1), host2 = x.AddFolder(host, BM::End, L"h2", 1);
        OK(!Accepts(x, deep, host2) && Commit(x, deep, {host2, BM::End, true}) == Result::Refused);
        OK(Accepts(x, deep, BM::BarRoot));
    }

    // ---- Drawer targets -----------------------------------------------------------------------------------------
    const std::vector<Row> rows{{BM::BarRoot, 0, true, true}, {a, 1}, {f, 1, true, true}, {inner, 2, true, false}, {b, 1}, {BM::OtherRoot, 0, true, true}, {o, 1}};
    auto l = ListTarget(s, o, rows, 1, .2f);  OK(l.drop.parent == BM::BarRoot && l.drop.index == 0 && l.mark == Mark::Before);
    l = ListTarget(s, o, rows, 1, .8f);       OK(l.drop.index == 1 && l.mark == Mark::After);
    l = ListTarget(s, o, rows, 2, .5f);       OK(l.drop.parent == f && l.drop.index == BM::End && l.mark == Mark::Into);
    l = ListTarget(s, o, rows, 2, .9f);       OK(l.drop.parent == f && l.drop.index == 0);                    // under an open folder: its first child
    l = ListTarget(s, o, rows, 3, .1f);       OK(l.drop.parent == f && l.drop.index == 0 && l.mark == Mark::Before);
    l = ListTarget(s, o, rows, 0, .5f);       OK(l.drop.parent == BM::BarRoot && l.drop.index == 0);          // the root label
    l = ListTarget(s, f, rows, 3, .5f);       OK(!l.drop.valid);                                               // a folder into its own child
    l = ListTarget(s, a, rows, 1, .5f);       OK(!l.drop.valid);                                               // onto itself
    l = ListTarget(s, o, rows, 99, .5f);      OK(!l.drop.valid);
    { BM::Store x = s; OK(Commit(x, o, ListTarget(x, o, rows, 2, .5f).drop) == Result::Moved && x.Find(o)->parent == f && x.Find(f)->children.back() == o); }
    { BM::Store x = s; OK(Commit(x, a, ListTarget(x, a, rows, 4, .8f).drop) == Result::Moved && x.Find(BM::BarRoot)->children.back() == a); }   // drawer → bar end
    std::printf("bookmark drag: %d checks passed\n", checks);
    return 0;
}
