#include "../src/bookmarks.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

// Checks stay active in every build (no NDEBUG dependency).
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

using namespace Bookmarks;

static std::string Text(const Store& s) { std::ostringstream o; s.Write(o); return o.str(); }
static Store Parse(const std::string& text, bool* damaged = nullptr) { std::istringstream in(text); return Store::Read(in, damaged); }
static Store RoundTrip(const Store& s, bool* damaged = nullptr) { return Parse(Text(s), damaged); }

int main() {
    // ---- Fixed roots ---------------------------------------------------------------------
    {
        Store s;
        CHECK(s.Size() == 0 && s.Find(BarRoot) && s.Find(OtherRoot) && !s.Find(NoId) && !s.Find(3));
        CHECK(s.Find(BarRoot)->kind == Kind::Folder && s.Find(BarRoot)->title == L"Barre de favoris" && s.Find(OtherRoot)->title == L"Autres favoris");
        CHECK(s.Depth(BarRoot) == 1 && s.Depth(OtherRoot) == 1 && s.Depth(99) == 0);
        CHECK(!s.Rename(BarRoot, L"x") && !s.Remove(BarRoot) && !s.Remove(OtherRoot) && !s.Move(BarRoot, OtherRoot, 0) && !s.Move(OtherRoot, BarRoot, 0));
        CHECK(s.NextId() == FirstUserId);
        CHECK(Text(s) == "browser-bookmarks 1\nend\t0\n");
        bool damaged = true; CHECK(RoundTrip(s, &damaged) == s && !damaged);
    }

    // ---- Add, ids, order, duplicates ----------------------------------------------------
    {
        Store s;
        const Id a = s.Add(BarRoot, End, L"A", L"https://a.example/", 10);
        const Id b = s.Add(BarRoot, 0, L"B", L"https://b.example/x", 20);
        const Id f = s.AddFolder(BarRoot, 1, L"Travail", 30);
        CHECK(a == 3 && b == 4 && f == 5 && s.NextId() == 6);
        CHECK((s.Find(BarRoot)->children == std::vector<Id>{b, f, a}));
        CHECK(s.IndexOf(a) == 2 && s.IndexOf(BarRoot) == End && s.IndexOf(42) == End);
        CHECK(s.Find(f)->kind == Kind::Folder && s.Find(f)->url.empty() && s.Find(a)->added == 10);
        // Index past the end appends.
        const Id c = s.Add(f, 999, L"", L"HTTPS://A.Example", 40);
        CHECK(c == 6 && s.Find(f)->children.back() == c && s.Root(c).id == BarRoot && s.Root(OtherRoot).id == OtherRoot && s.Root(12345).id == OtherRoot);
        // Duplicates are allowed (the host decides); Matching finds all, newest first, by normalized key.
        CHECK((s.Matching(L"https://a.example") == std::vector<Id>{c, a}));
        CHECK((s.Matching(L"https://A.EXAMPLE/") == std::vector<Id>{c, a}));
        CHECK(s.Matching(L"https://a.example/#top").empty() && s.Matching(L"").empty() && s.Matching(L"http://a.example/").empty());
        CHECK(DisplayTitle(*s.Find(c)) == L"a.example" && DisplayTitle(*s.Find(a)) == L"A");
        CHECK(s.CountLinks(f) == 1 && s.CountLinks(BarRoot) == 3 && s.CountLinks(a) == 1 && s.CountLinks(77) == 0);
        // Into a link, unknown parent, refused schemes, oversize URL: nothing created, no id consumed.
        const Store before = s;
        CHECK(s.Add(a, 0, L"x", L"https://x.example/", 1) == NoId && s.Add(99, 0, L"x", L"https://x.example/", 1) == NoId && s.AddFolder(a, 0, L"x", 1) == NoId);
        for (const wchar_t* bad : {L"javascript:alert(1)", L"data:text/html,<b>x</b>", L"about:blank", L"blob:https://x/1", L"edge://settings", L"chrome://flags", L"view-source:https://x/", L"ftp://x.example/", L"", L"example.com"})
            CHECK(s.Add(BarRoot, End, L"bad", bad, 1) == NoId);
        CHECK(s.Add(BarRoot, End, L"long", L"https://x.example/" + std::wstring(5000, L'a'), 1) == NoId);
        CHECK(s == before && s.NextId() == 7);
        // Local files are accepted.
        CHECK(s.Add(OtherRoot, End, L"notes", L"file:///C:/notes%20x.html", 50) != NoId);
    }

    // ---- Rename / SetUrl / titles ------------------------------------------------------------
    {
        Store s;
        const Id a = s.Add(BarRoot, End, L"A", L"https://a.example/", 1);
        const Id f = s.AddFolder(BarRoot, End, L"F", 1);
        CHECK(s.Rename(a, L"Été 🌍") && s.Find(a)->title == L"Été 🌍" && s.Rename(f, L"") && s.Find(f)->title.empty());
        CHECK(!s.Rename(99, L"x"));
        CHECK(s.SetUrl(a, L"http://b.example/p?q=1#h") && s.Find(a)->url == L"http://b.example/p?q=1#h");
        CHECK(!s.SetUrl(a, L"javascript:void(0)") && s.Find(a)->url == L"http://b.example/p?q=1#h");
        CHECK(!s.SetUrl(f, L"https://a.example/") && s.Find(f)->url.empty() && !s.SetUrl(99, L"https://a.example/"));
        // Titles are clipped to TitleMax without splitting a surrogate pair.
        std::wstring longTitle(Store::TitleMax - 1, L'x'); longTitle += L"😀tail";
        CHECK(s.Rename(a, longTitle) && s.Find(a)->title.size() == Store::TitleMax - 1);
        CHECK(s.Rename(a, std::wstring(Store::TitleMax + 50, L'y')) && s.Find(a)->title.size() == Store::TitleMax);
    }

    // ---- Round trip: 3 levels, accents, emoji, newlines, tabs, backslashes, lone surrogates ---------
    {
        Store s;
        const Id f1 = s.AddFolder(BarRoot, End, L"Dossier « été »", 100);
        const Id f2 = s.AddFolder(f1, End, L"Niveau\t2\\n (pas un saut)", 200);
        const Id l1 = s.Add(f2, End, L"Ligne 1\nLigne 2\r\nFin\\", L"file:///C:/notes%20x.html", 300);
        const Id l2 = s.Add(f2, 0, L"日本語 🚀 \u200F\u202E rtl", L"https://例え.jp/パス?q=ü#frag", 400);
        std::wstring lone = L"lone "; lone += static_cast<wchar_t>(0xD83D); lone += L" and "; lone += static_cast<wchar_t>(0xDE00); lone += L"\\u0041";
        const Id l3 = s.Add(OtherRoot, End, lone, L"https://x.example/a\\b", -5);
        const Id l4 = s.Add(BarRoot, 0, L"", L"http://plain.example", 0);
        bool damaged = true;
        const Store back = RoundTrip(s, &damaged);
        CHECK(!damaged && back == s && back.NextId() == s.NextId());
        CHECK(back.Find(l1)->title == L"Ligne 1\nLigne 2\r\nFin\\" && back.Find(l2)->url == L"https://例え.jp/パス?q=ü#frag");
        CHECK(back.Find(l3)->title == lone && back.Find(l3)->added == -5 && back.Find(l4)->title.empty());
        CHECK((back.Find(f2)->children == std::vector<Id>{l2, l1}) && back.Depth(l1) == 4 && back.Find(f2)->title == L"Niveau\t2\\n (pas un saut)");
        // One physical line per node, plus header and trailer.
        const std::string text = Text(s);
        CHECK(std::count(text.begin(), text.end(), '\n') == 2 + 6);
        CHECK(text.find("Ligne 1\\nLigne 2\\r\\nFin\\\\") != std::string::npos && text.find("\\uD83D") != std::string::npos);
        // Writing twice is stable; CRLF line ends (edited by hand on Windows) still read.
        CHECK(Text(back) == text);
        std::string crlf; for (char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
        CHECK(Parse(crlf, &damaged) == s && !damaged);
        // Escaping helpers on their own.
        std::wstring u; CHECK(Unescape(Escape(lone), u) && u == lone);
        for (const char* bad : {"a\\", "\\x", "\\u12", "\\u0041", "\\uZZZZ", "a\tb"}) CHECK(!Unescape(bad, u));
    }

    // ---- Move / reorder, cycles and depth ----------------------------------------------------
    {
        Store s;
        const Id a = s.Add(BarRoot, End, L"a", L"https://a.example/", 1);
        const Id b = s.Add(BarRoot, End, L"b", L"https://b.example/", 2);
        const Id c = s.Add(BarRoot, End, L"c", L"https://c.example/", 3);
        const Id f = s.AddFolder(BarRoot, End, L"F", 4);
        const Id g = s.AddFolder(f, End, L"G", 5);
        const Id h = s.AddFolder(g, End, L"H", 6);
        // Reorder in place: index is the final position.
        CHECK(s.Move(a, BarRoot, 2) && (s.Find(BarRoot)->children == std::vector<Id>{b, c, a, f}));
        CHECK(s.Move(a, BarRoot, 0) && (s.Find(BarRoot)->children == std::vector<Id>{a, b, c, f}));
        CHECK(s.Move(f, BarRoot, End) && s.Find(BarRoot)->children.back() == f);
        CHECK(s.Move(c, BarRoot, 1) && (s.Find(BarRoot)->children == std::vector<Id>{a, c, b, f}));
        // Across folders.
        CHECK(s.Move(b, g, 0) && s.Find(b)->parent == g && (s.Find(g)->children == std::vector<Id>{b, h}) && s.IndexOf(b) == 0);
        CHECK(s.Move(b, OtherRoot, 50) && s.Root(b).id == OtherRoot);
        CHECK(s.IsAncestor(f, h) && s.IsAncestor(BarRoot, h) && !s.IsAncestor(h, f) && !s.IsAncestor(f, f) && !s.IsAncestor(OtherRoot, h) && !s.IsAncestor(99, h));
        CHECK(s.FolderHeight(f) == 3 && s.FolderHeight(h) == 1 && s.FolderHeight(a) == 0);
        // Refused moves leave the tree fully unchanged.
        const Store before = s;
        CHECK(!s.Move(f, h, 0) && !s.Move(f, g, 0) && !s.Move(f, f, 0) && !s.Move(BarRoot, f, 0) && !s.Move(OtherRoot, BarRoot, 0));
        CHECK(!s.Move(a, c, 0) && !s.Move(a, 99, 0) && !s.Move(99, BarRoot, 0) && !s.Move(a, NoId, 0));
        CHECK(s == before);
        // Depth: folders up to level 8 (roots = 1); the 9th level is refused.
        Store d; Id parent = BarRoot; std::vector<Id> chain;
        for (int level = 2; level <= 8; ++level) { parent = d.AddFolder(parent, End, L"L" + std::to_wstring(level), 0); CHECK(parent != NoId && d.Depth(parent) == level); chain.push_back(parent); }
        CHECK(d.AddFolder(parent, End, L"L9", 0) == NoId);
        CHECK(d.Add(parent, End, L"leaf", L"https://leaf.example/", 0) != NoId); // a link inside the deepest folder is fine
        // Moving a 3-level folder under level 6 would reach 9: refused. Under level 5: allowed (5+3=8).
        const Id t = d.AddFolder(OtherRoot, End, L"T", 0); const Id t2 = d.AddFolder(t, End, L"T2", 0); d.AddFolder(t2, End, L"T3", 0);
        const Store dBefore = d;
        CHECK(!d.Move(t, chain[4], 0) && d == dBefore);
        CHECK(d.Move(t, chain[3], 0) && d.Depth(t) == 6 && d.FolderHeight(t) == 3);
        bool damaged = true; CHECK(RoundTrip(d, &damaged) == d && !damaged);
    }

    // ---- Remove + Restore (undo) -------------------------------------------------------------
    {
        Store s;
        const Id first = s.Add(BarRoot, End, L"first", L"https://first.example/", 1);
        const Id work = s.AddFolder(BarRoot, End, L"Travail", 2);
        const Id last = s.Add(BarRoot, End, L"last", L"https://last.example/", 3);
        const Id sub = s.AddFolder(work, End, L"Sub", 4);
        for (int i = 0; i < 12; ++i) s.Add(i % 2 ? work : sub, End, L"p" + std::to_wstring(i), L"https://p.example/" + std::to_wstring(i), 10 + i);
        CHECK(s.CountLinks(work) == 12 && s.CountLinks(BarRoot) == 14);
        const Store snapshot = s; const Id nextBefore = s.NextId();
        auto removed = s.Remove(work);
        CHECK(removed && removed->parent == BarRoot && removed->index == 1 && removed->subtree.size() == 14 && removed->subtree[0].id == work);
        CHECK(!s.Find(work) && !s.Find(sub) && s.Size() == 2 && (s.Find(BarRoot)->children == std::vector<Id>{first, last}));
        // Ids are never reused after a removal.
        const Id fresh = s.Add(BarRoot, End, L"fresh", L"https://fresh.example/", 99);
        CHECK(fresh == nextBefore && fresh > work);
        auto freshRemoved = s.Remove(fresh); CHECK(freshRemoved);
        // Restore: same ids, same place, same order.
        CHECK(s.Restore(*removed) && s == snapshot);
        // A second Restore of the same snapshot is refused (ids in use).
        CHECK(!s.Restore(*removed) && s == snapshot);
        // Removing a link and undoing puts it back at its index.
        auto lr = s.Remove(last); CHECK(lr && lr->index == 2 && lr->subtree.size() == 1);
        CHECK(s.Restore(*lr) && s.IndexOf(last) == 2);
        // Parent removed in the meantime: the subtree lands at the end of OtherRoot.
        const Id other = s.Add(OtherRoot, End, L"o", L"https://o.example/", 1);
        auto inner = s.Remove(sub); CHECK(inner && inner->parent == work);
        auto outer = s.Remove(work); CHECK(outer);
        CHECK(s.Restore(*inner) && s.Find(sub)->parent == OtherRoot && (s.Find(OtherRoot)->children == std::vector<Id>{other, sub}));
        CHECK(s.CountLinks(sub) == 6);
        // The outer folder still restores (its snapshot no longer contains `sub`).
        CHECK(s.Restore(*outer) && s.IndexOf(work) == 1 && s.CountLinks(work) == 6);
        bool damaged = true; CHECK(RoundTrip(s, &damaged) == s && !damaged);
        // Index clamped when the parent shrank meanwhile.
        auto tail = s.Remove(last); CHECK(tail && tail->index == 2);
        s.Remove(first); s.Remove(work);
        CHECK(s.Restore(*tail) && s.IndexOf(last) == 0);
        // Inconsistent snapshots are refused without changing anything.
        Store t; const Id tf = t.AddFolder(BarRoot, End, L"F", 0); t.Add(tf, End, L"x", L"https://x.example/", 0);
        auto good = t.Remove(tf); CHECK(good);
        const Store empty = t;
        Removed broken = *good; broken.subtree[1].parent = 999; CHECK(!t.Restore(broken) && t == empty);
        broken = *good; broken.subtree[1].url = L"javascript:alert(1)"; CHECK(!t.Restore(broken) && t == empty);
        broken = *good; broken.subtree[0].children.push_back(broken.subtree[1].id); CHECK(!t.Restore(broken) && t == empty);
        broken = *good; broken.subtree[0].children.clear(); CHECK(!t.Restore(broken) && t == empty);
        broken = *good; broken.subtree[0].id = BarRoot; CHECK(!t.Restore(broken) && t == empty);
        CHECK(!t.Restore(Removed{}) && t == empty);
        CHECK(t.Restore(*good) && t.CountLinks(tf) == 1);
        // A restored folder that no longer fits in depth at its old parent goes to OtherRoot.
        Store d; Id p = BarRoot; std::vector<Id> chain;
        for (int level = 2; level <= 5; ++level) { p = d.AddFolder(p, End, L"x", 0); chain.push_back(p); }
        const Id deep = d.AddFolder(chain[3], End, L"deep", 0); d.AddFolder(deep, End, L"deeper", 0); // levels 6, 7
        auto deepRemoved = d.Remove(deep); CHECK(deepRemoved);
        const Id wrap = d.AddFolder(BarRoot, 0, L"wrap", 0); CHECK(d.Move(chain[0], wrap, 0)); // chain[3] is now level 6
        const Id wrap2 = d.AddFolder(BarRoot, 0, L"wrap2", 0); CHECK(d.Move(wrap, wrap2, 0)); // chain[3] is now level 7
        CHECK(d.Depth(chain[3]) == 7);
        CHECK(d.Restore(*deepRemoved) && d.Find(deep)->parent == OtherRoot && d.Depth(deep) == 2 && d.FolderHeight(deep) == 2);
    }

    // ---- Limit -----------------------------------------------------------------------------------
    {
        Store s;
        for (std::size_t i = 0; i < Store::Limit; ++i) CHECK(s.Add(OtherRoot, End, L"", L"https://n.example/" + std::to_wstring(i), 0) != NoId);
        CHECK(s.Add(OtherRoot, End, L"", L"https://over.example/", 0) == NoId && s.AddFolder(OtherRoot, End, L"over", 0) == NoId);
        auto r = s.Remove(s.Find(OtherRoot)->children[5]); CHECK(r);
        const Id refill = s.Add(BarRoot, End, L"", L"https://refill.example/", 0); CHECK(refill != NoId);
        CHECK(!s.Restore(*r)); // would pass the limit
        bool damaged = true;
        const auto t0 = std::chrono::steady_clock::now();
        const Store back = RoundTrip(s, &damaged);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(back == s && !damaged);
        std::printf("10000-node round trip: %lld ms\n", static_cast<long long>(ms));
        // A file with more than Limit nodes: the excess is dropped and reported.
        std::string text = Text(s); text.erase(text.rfind("end\t"));
        text += "99999\t2\tL\t0\t\thttps://excess.example/\nend\t10001\n";
        const Store capped = Parse(text, &damaged);
        CHECK(damaged && capped.Size() == Store::Limit && !capped.Find(99999));
    }

    // ---- Corrupt, foreign and truncated files ------------------------------------------------------
    {
        bool damaged = true;
        // Missing file (empty stream): empty, not damaged.
        CHECK(Parse("", &damaged) == Store{} && !damaged);
        // Foreign or future header: empty and damaged (the host keeps a .damaged copy).
        for (const char* bad : {"garbage\n", "browser-bookmarks 2\n3\t1\tL\t0\tx\thttps://x.example/\nend\t1\n", "browser-history 1\n", "\xEF\xBB\xBF" "browser-bookmarks 1\nend\t0\n", "browser-bookmarks 1"}) {
            damaged = false; CHECK(Parse(bad, &damaged) == Store{} && damaged);
        }
        // Reference file.
        Store ref;
        const Id f = ref.AddFolder(BarRoot, End, L"F", 1);
        const Id a = ref.Add(f, End, L"a", L"https://a.example/", 2);
        const Id b = ref.Add(BarRoot, End, L"b", L"https://b.example/", 3);
        const std::string good = Text(ref);
        CHECK(Parse(good, &damaged) == ref && !damaged);
        // Truncated mid-line: complete lines are kept, the partial line and missing trailer flag damage.
        const std::string cut = good.substr(0, good.find("https://b.example") + 10);
        Store t = Parse(cut, &damaged);
        CHECK(damaged && t.Find(f) && t.Find(a) && !t.Find(b));
        // Truncated exactly at a line end (trailer lost): everything kept but damaged.
        t = Parse(good.substr(0, good.find("end\t")), &damaged);
        CHECK(damaged && t == ref);
        // Wrong trailer count, data after the trailer.
        std::string wrong = good; wrong.replace(wrong.find("end\t3"), 5, "end\t4");
        CHECK(Parse(wrong, &damaged) == ref && damaged);
        CHECK(Parse(good + "9\t1\tL\t0\tlate\thttps://late.example/\n", &damaged) == ref && damaged);
        CHECK(Parse(good.substr(0, good.size() - 1), &damaged) == ref && damaged); // trailer cut before its newline
        // Malformed lines are skipped one by one.
        const char* badLines[] = {
            "10\t1\tL\t0\tx\n",                                  // too few fields
            "10\t1\tL\t0\tx\thttps://x.example/\textra\n",        // too many fields
            "x10\t1\tL\t0\tx\thttps://x.example/\n",              // non-numeric id
            "-10\t1\tL\t0\tx\thttps://x.example/\n",              // signed id
            "10\t1\tQ\t0\tx\thttps://x.example/\n",               // unknown kind
            "10\t1\tL\t1.5\tx\thttps://x.example/\n",             // bad timestamp
            "10\t1\tL\t99999999999999999999\tx\thttps://x.example/\n", // timestamp overflow
            "1\t0\tF\t0\troot\t\n",                               // reserved id
            "0\t1\tL\t0\tzero\thttps://x.example/\n",             // id 0
            "4294967295\t1\tL\t0\tmax\thttps://x.example/\n",     // max id (no next id)
            "99999999999\t1\tL\t0\tbig\thttps://x.example/\n",    // id overflow
            "10\t1\tL\t0\tx\tjavascript:alert(1)\n",              // refused scheme
            "10\t1\tL\t0\tx\tdata:text/html,hi\n",
            "10\t1\tL\t0\tx\tabout:blank\n",
            "10\t1\tF\t0\tfolder with url\thttps://x.example/\n", // folder carrying a URL
            "10\t1\tL\t0\tbad \xC3\x28 utf8\thttps://x.example/\n", // invalid UTF-8
            "10\t1\tL\t0\tbad escape \\q\thttps://x.example/\n",   // unknown escape
            "3\t1\tL\t0\tduplicate id\thttps://dup.example/\n",   // duplicate of an existing id
        };
        for (const char* line : badLines) {
            std::string text = good; text.insert(text.find("end\t"), line);
            text.replace(text.find("end\t3"), 5, "end\t4");
            const Store r = Parse(text, &damaged);
            if (!(damaged && r == ref)) std::fprintf(stderr, "line: %s", line);
            CHECK(damaged && r == ref);
        }
        // Oversized URL line.
        { std::string text = good; text.insert(text.find("end\t"), "10\t1\tL\t0\tx\thttps://x.example/" + std::string(5000, 'a') + "\n");
          text.replace(text.find("end\t3"), 5, "end\t4"); CHECK(Parse(text, &damaged) == ref && damaged); }
        // Oversized title: kept, clipped, reported.
        { std::string text = good; text.insert(text.find("end\t"), "10\t1\tL\t0\t" + std::string(3000, 't') + "\thttps://x.example/\n");
          text.replace(text.find("end\t3"), 5, "end\t4"); const Store r = Parse(text, &damaged);
          CHECK(damaged && r.Find(10) && r.Find(10)->title.size() == Store::TitleMax); }
        // Child before its parent, and a cycle 5 -> 6 -> 5: no loop, faulty nodes in OtherRoot, damaged.
        const std::string cyclic = "browser-bookmarks 1\n"
            "7\t8\tL\t0\tchild first\thttps://c.example/\n"
            "8\t1\tF\t0\tparent later\t\n"
            "5\t6\tF\t0\tfive\t\n"
            "6\t5\tF\t0\tsix\t\n"
            "9\t5\tL\t0\tin five\thttps://five.example/\n"
            "11\t9\tL\t0\tparent is a link\thttps://l.example/\n"
            "end\t6\n";
        const Store cy = Parse(cyclic, &damaged);
        CHECK(damaged && cy.Size() == 6);
        CHECK(cy.Find(7)->parent == OtherRoot && cy.Find(8)->parent == BarRoot && cy.Find(5)->parent == OtherRoot && cy.Find(6)->parent == 5 && cy.Find(9)->parent == 5 && cy.Find(11)->parent == OtherRoot);
        CHECK(!cy.IsAncestor(6, 5) && cy.IsAncestor(5, 6) && cy.NextId() == 12);
        // Its rewrite is clean and stable.
        CHECK(RoundTrip(cy, &damaged) == cy && !damaged);
        // 12 nested folders on disk: brought back to 8 levels.
        std::string nested = "browser-bookmarks 1\n"; Id parent = BarRoot;
        for (Id id = 10; id < 22; ++id) { nested += std::to_string(id) + "\t" + std::to_string(parent) + "\tF\t0\tL\t\n"; parent = id; }
        nested += "50\t21\tL\t0\tleaf\thttps://leaf.example/\nend\t13\n";
        const Store deep = Parse(nested, &damaged);
        CHECK(damaged && deep.Size() == 13);
        int maxFolderDepth = 0;
        for (Id id = 10; id < 22; ++id) maxFolderDepth = std::max(maxFolderDepth, deep.Depth(id));
        CHECK(maxFolderDepth == static_cast<int>(Store::MaxDepth) && deep.Find(16)->parent == 15 && deep.Depth(16) == 8 && deep.Find(17)->parent == OtherRoot && deep.Find(18)->parent == 17 && deep.Find(50)->parent == 21);
        // 10 000 lines that each name an unknown parent: linear, no loop.
        std::string flood = "browser-bookmarks 1\n";
        for (Id id = 3; id < 3 + Store::Limit; ++id) flood += std::to_string(id) + "\t" + std::to_string(id + 1) + "\tF\t0\tf\t\n";
        flood += "end\t" + std::to_string(Store::Limit) + "\n";
        const auto t0 = std::chrono::steady_clock::now();
        const Store fl = Parse(flood, &damaged);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("10000 orphan lines: %lld ms\n", static_cast<long long>(ms));
        CHECK(damaged && fl.Size() == Store::Limit && fl.Find(OtherRoot)->children.size() == Store::Limit && ms < 2000);
        // Ids keep growing past the largest one read.
        Store grow = Parse("browser-bookmarks 1\n500\t1\tL\t0\tx\thttps://x.example/\nend\t1\n", &damaged);
        CHECK(!damaged && grow.NextId() == 501 && grow.Add(BarRoot, End, L"y", L"https://y.example/", 0) == 501);
    }

    std::puts("bookmarks tests passed");
    return 0;
}
