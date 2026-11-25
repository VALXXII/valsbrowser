// Split view host behaviour through the real window procedure of src/main.cpp:
// lift vs reorder, drop targets, insertion and refusal, Escape restore, menu path,
// keyboard resize, rails on shrink, close without losing tabs, session groups.
// Tabs carry no WebView2 controller here (no runtime, no network); page placement is
// observed through the same state ApplyPages() computes for the real controllers.
#include "../../src/main.cpp"
#include <cassert>
#include <cstdio>

static LPARAM At(float x, float y) { return MAKELPARAM((int)std::lround(x), (int)std::lround(y)); }
static void Drag(const std::shared_ptr<Tab>& t, float x, float y, bool release = true) {
    const int i = TabIndexOf(t); chrome.Build((int)pages.size(), current); auto s = chrome.strip.Surface(i);
    WndProc(window, WM_LBUTTONDOWN, MK_LBUTTON, At(s.left + 20, 20));
    WndProc(window, WM_MOUSEMOVE, MK_LBUTTON, At(s.left + 40, 30));
    WndProc(window, WM_MOUSEMOVE, MK_LBUTTON, At(x, 70));     // tension band: still the strip
    WndProc(window, WM_MOUSEMOVE, MK_LBUTTON, At(x, y));
    if (release) WndProc(window, WM_LBUTTONUP, 0, At(x, y));
}
static std::shared_ptr<Tab> MakeTab(const wchar_t* title) { auto t = std::make_shared<Tab>(); t->title = title; t->home = false; t->uri = std::wstring(L"https://") + title + L".test/"; t->revealed = true; return t; }
static int Shown() { int n = 0; for (auto& t : pages) n += t->shown; return n; }

int main() {
    window = CreateWindowExW(0, L"STATIC", L"split test", WS_POPUP, 0, 0, 1180, 815, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(window); chrome.width = 1180; chrome.height = 815;
    auto yt = MakeTab(L"Youtube"), luma = MakeTab(L"Luma"), paper = MakeTab(L"Paper"), gh = MakeTab(L"GitHub");
    pages = {yt, luma, paper}; current = 0; yt->usedAt = ++useClock; ApplyPages();
    assert(Shown() == 1 && yt->shown && !chrome.split.shown);

    // The bookmarks band is chrome, not a split drop zone. Lifting begins below
    // the current page edge, including when the band changes the content height.
    settings.bookmarksBar=BrowserSettings::BookmarksBar::Always;BuildChrome();ApplyPages();
    assert(ContentBox().top==121);
    Drag(paper,300,100,false);assert(!tabDrag.lifted&&splitGroups.empty());
    CancelTabDrag(true);
    settings.bookmarksBar=BrowserSettings::BookmarksBar::Auto;BuildChrome();ApplyPages();
    assert(ContentBox().top==81);

    // 1. Reorder stays in the strip: a drop above the content never splits.
    Drag(paper, 60, 20); assert(splitGroups.empty() && !tabDrag.tab);

    // 2. Lift Paper below the address row onto the right of the page: side-by-side pair.
    pages = {yt, luma, paper}; current = 0; ApplyPages();
    Drag(paper, 1000, 450, false);
    assert(tabDrag.lifted && chrome.liftedIndex == 2 && tabDrag.target.kind == SV::Kind::Split && tabDrag.target.side == SV::Side::Right);
    assert(!chrome.split.ring);  // one accent at a time while previewing
    WndProc(window, WM_LBUTTONUP, 0, At(1000, 450));
    assert(splitGroups.size() == 1 && yt->group && yt->group == paper->group && !luma->group);
    assert(pages[0] == yt && pages[1] == paper && pages[2] == luma);      // the pair sits together, Youtube first
    assert(Active() == paper && yt->shown && paper->shown && !luma->shown && Shown() == 2);
    assert(chrome.split.shown && chrome.split.ring && chrome.split.handles.size() == 1);
    assert(chrome.split.active.left >= 590 && chrome.split.active.right == 1176);  // Paper on the right (Paper V3: 2 DIP outer outline; #127: 4 DIP inset)
    // the separator grip: 16 DIP centred on the 4 DIP gap (6 DIP over each page edge); z-order above the
    // real pages is checked by the native --split-test, where WebView2 children exist
    { auto sv = CurrentSplit(); auto g = sv.a.SeparatorBox(sv.a.separators[0]); assert(std::fabs(g.right - g.left - 4) < .01f);
      // Layered child windows need a Windows 8+ manifest (browser.exe has one, this fixture does not):
      // without it no grip exists and the 4 DIP gap alone stays the pointer target (the handle is then painted in the gap).
      if (!splitGrips.empty()) { assert(GetWindowLongW(splitGrips[0], GWL_STYLE) & WS_VISIBLE);
        RECT r; GetWindowRect(splitGrips[0], &r); POINT o{0, 0}; ClientToScreen(window, &o);
        assert(r.right - r.left == 16 && std::abs((r.left - o.x) - (LONG)std::lround(g.left - 6)) <= 1); } }
    // at rest only the thin gap shows: no handle drawn (hover, drag or keyboard focus bring it)
    assert(chrome.split.hotHandle < 0 && chrome.split.paintHandles == splitGrips.empty()); for (auto& L : gripLooks) assert(L.target == 0 && L.alpha == 0);
    // the preview chip names only the dragged tab
    assert(splitAnim.frame.plate.detail.empty());
    assert(chrome.tabGroups[0] && chrome.tabGroups[0] == chrome.tabGroups[1] && !chrome.tabGroups[2]);
    // the group on screen is the one whose members get a surface in the strip (Paper 08)
    assert(chrome.visibleTabGroup == chrome.tabGroups[0]);
    // active panel ring: neutral; a focus taken without a mouse button down (Tab, F6) is keyboard
    // modality and shows the strong ring, a click the discreet one; hover never activates
    ActivatePanel(yt); assert(Active() == yt && keyboardModality && chrome.split.ring);
    keyboardModality = false; ActivatePanel(yt); assert(keyboardModality);  // re-focus by keyboard re-arms it
    ActivatePanel(paper); assert(Active() == paper);
    // the address bar follows the active panel: Active() drives it
    assert(Active()->uri == L"https://Paper.test/");

    // Drag surfaces use the compact site name; the tab keeps its full document title.
    { const std::wstring full = L"GitHub · Le changement est constant. GitHub vous permet de garder une longueur d’avance. · GitHub · GitHub";
      const std::wstring oldTitle = luma->title, oldUri = luma->uri; luma->title = full; luma->uri = L"https://github.com/?locale=fr-fr";
      Drag(luma, 300, 450, false); assert(tabDrag.lifted && splitAnim.frame.card.title == L"GitHub");
      if (tabDrag.target.kind != SV::Kind::None && tabDrag.target.kind != SV::Kind::Refuse) assert(splitAnim.frame.plate.title == L"GitHub");
      WndProc(window, WM_KEYDOWN, VK_ESCAPE, 0); assert(luma->title == full && pages[2] == luma);
      luma->title = oldTitle; luma->uri = oldUri; }

    // 3. Escape while lifted restores everything, including the strip order.
    Drag(luma, 300, 450, false); assert(tabDrag.lifted);
    WndProc(window, WM_KEYDOWN, VK_ESCAPE, 0);
    assert(!tabDrag.tab && pages[2] == luma && !luma->group && Members(yt->group).size() == 2 && chrome.liftedIndex == -1);

    // 4. Insert a third site between the two panels (outer quarter of a panel).
    Drag(luma, 560, 450);
    assert(Members(yt->group).size() == 3 && pages[0] == yt && pages[1] == luma && pages[2] == paper && Active() == luma);
    for (auto& t : {yt, luma, paper}) assert(t->shown);
    float total = yt->weight + luma->weight + paper->weight; assert(std::fabs(luma->weight / total - 1.f / 3) < .02f);

    // 5. A fourth site would fall under 360 px at 1180 DIP: refused, nothing changes.
    pages.push_back(gh); gh->usedAt = 0; ApplyPages();
    auto v = CurrentSplit(); const float boundary = v.a.slots[1].box.right - 6;
    Drag(gh, boundary, 450, false);
    assert(tabDrag.target.kind == SV::Kind::Refuse && tabDrag.target.needed == 4 && splitAnim.frame.pill.shown);
    WndProc(window, WM_LBUTTONUP, 0, At(boundary, 450));
    assert(!gh->group && Members(yt->group).size() == 3 && pages.back() == gh);
    // ...but replacing a panel (its middle) is allowed: the replaced site becomes a tab again.
    v = CurrentSplit(); Drag(gh, (v.a.slots[0].box.left + v.a.slots[0].box.right) / 2, 450);
    assert(gh->group == luma->group && !yt->group && Members(gh->group).size() == 3 && Active() == gh);
    assert(std::find(pages.begin(), pages.end(), yt) != pages.end() && pages.size() == 4);  // nothing closed

    // 6. Window narrows: the least recent site collapses into a rail, the active one never.
    for (auto& t : pages) t->usedAt = 0; luma->usedAt = 1; paper->usedAt = 2; gh->usedAt = 3; current = TabIndexOf(gh);
    chrome.width = 900; ApplyPages();
    assert(chrome.split.rails.size() == 1 && gh->shown && paper->shown && !luma->shown);
    assert(pages.size() == 4 && Members(gh->group).size() == 3);  // still in the group, not closed
    // clicking the rail brings that site back (it becomes active)
    auto rail = chrome.split.rails[0]; WndProc(window, WM_LBUTTONDOWN, MK_LBUTTON, At((rail.left + rail.right) / 2, 300));
    assert(Active() == luma && luma->shown);
    chrome.width = 1180; ApplyPages(); assert(chrome.split.rails.empty() && Shown() == 3);

    // 7. Keyboard resize: arrows move the separator by 16 DIP, never under the minimum.
    StartKeyboardResize(); assert(splitResize >= 0 && chrome.split.focusedHandle == splitResize);
    if (!gripLooks.empty()) assert(gripLooks[splitResize].target == 1 && gripLooks[splitResize].ring);  // keyboard: handle + strong ring
    v = CurrentSplit(); const int s = v.a.separators[splitResize]; const float before = SV::Extent(v.a.slots[s].box, v.a.orientation);
    // The step reads Shift through GetKeyState: pin this thread's key state, or a Shift held in
    // another application while the test runs turns 16 DIP into 64.
    { BYTE keys[256]; GetKeyboardState(keys); keys[VK_SHIFT] = keys[VK_LSHIFT] = keys[VK_RSHIFT] = 0; SetKeyboardState(keys); }
    WndProc(window, WM_KEYDOWN, VK_LEFT, 0); v = CurrentSplit();

    assert(std::fabs(SV::Extent(v.a.slots[s].box, v.a.orientation) - (before - 16)) <= 1.5f);
    for (int i = 0; i < 20; ++i) WndProc(window, WM_KEYDOWN, VK_LEFT, 0);
    v = CurrentSplit(); for (auto& slot : v.a.slots) assert(slot.box.W() >= 360 - .5f);
    WndProc(window, WM_KEYDOWN, VK_RETURN, 0); v = CurrentSplit();   // Enter: equal shares
    assert(std::fabs(v.a.slots[0].box.W() - v.a.slots[2].box.W()) <= 1.5f);
    WndProc(window, WM_KEYDOWN, VK_ESCAPE, 0); assert(splitResize == -1);

    // 8. Separator by pointer: 1:1 drag, release snaps; double-click equalises.
    v = CurrentSplit(); auto sep = v.a.SeparatorBox(v.a.separators[0]); const float sx = (sep.left + sep.right) / 2;
    WndProc(window, WM_LBUTTONDOWN, MK_LBUTTON, At(sx, 400)); assert(sepDrag.active);
    WndProc(window, WM_MOUSEMOVE, MK_LBUTTON, At(sx + 10, 400)); v = CurrentSplit();
    assert(v.a.slots[0].box.right > sep.left + 5);
    WndProc(window, WM_LBUTTONUP, 0, At(sx + 10, 400)); assert(!sepDrag.active);

    // 9. Closing the active panel hands over to its neighbour; the last two keep the group.
    current = TabIndexOf(gh); ApplyPages(); const int groupId = gh->group;
    CloseAt(current); assert(Members(groupId).size() == 2 && Active()->group == groupId && Active()->shown);
    CloseAt(current); assert(splitGroups.empty() && Shown() <= 1);       // one left: back to a single page
    assert(pages.size() == 2);                                            // only the two closed tabs are gone

    // 10. Menu path (WCAG 2.5.7): stack a tab under the active one without dragging.
    pages = {yt, luma}; for (auto& t : pages) { t->group = 0; t->weight = 1; } current = 0; ApplyPages();
    PlaceFromMenu(luma, SV::Side::Bottom);
    assert(luma->group && luma->group == yt->group && splitGroups[luma->group] == SV::Orientation::Column && Active() == luma);
    v = CurrentSplit(); assert(v.a.orientation == SV::Orientation::Column && v.a.slots[1].box.top > v.a.slots[0].box.bottom);
    // stacking needs two 256 px heights: refused when the window is too low
    chrome.height = 500; v = CurrentSplit(); assert(v.a.visible == 1);  // collapsed, nothing lost
    chrome.height = 815; ReverseGroup(luma->group); assert(pages[0] == luma && pages[1] == yt);
    // F6 walks the panels in order before reaching the toolbar
    auto panels = ShownPanels(); assert(panels.size() == 2);

    // 11. Session format 2 keeps the group, its orientation and weights.
    luma->weight = 3; auto groups = SessionGroups(); assert(groups.size() == 1 && groups[0].orientation == 1 && groups[0].members == std::vector<int>({0, 1}) && groups[0].weights[0] == 3);
    QuitGroup(luma->group); assert(splitGroups.empty() && !luma->group && !yt->group && pages.size() == 2);
    ApplySessionGroups(groups); assert(splitGroups.size() == 1 && luma->group == yt->group && luma->weight == 3);

    // 12. One tab alone never lifts (no partner): it stays a reorder.
    pages = {yt}; yt->group = 0; splitGroups.clear(); current = 0; ApplyPages();
    Drag(yt, 600, 450, false); assert(!tabDrag.lifted); WndProc(window, WM_LBUTTONUP, 0, At(600, 450));

    // 13. Tab menu, split block V2 (Paper p-7-0): a stated target, positions that run, reasons.
    {
        auto find = [](const std::vector<CM::Item>& items, const std::wstring& label) -> const CM::Item* { for (auto& i : items) if (i.label == label) return &i; return nullptr; };
        auto run = [](const SplitMenu& m, const CM::Item* item, const std::shared_ptr<Tab>& source) { assert(item && item->enabled && item->id >= SplitMenuBase); RunSplitMenuAction(source, m.actions[item->id - SplitMenuBase]); };
        splitGroups.clear(); for (auto& t : pages) t->group = 0;
        auto lu = MakeTab(L"Luma");  // fresh: the earlier close test left Luma closed
        // one tab: no block at all
        pages = {yt}; current = 0; ApplyPages(); assert(BuildSplitMenu(yt).items.empty());
        // free tabs: the target is the page on screen, named once in a read-only header, short name
        auto gh1 = MakeTab(L"ValXXII/Browser"), gh2 = MakeTab(L"Issues · ValXXII/Browser");
        gh1->uri = L"https://github.com/ValXXII/Browser"; gh2->uri = L"https://github.com/ValXXII/Browser/issues";
        gh1->title = L"GitHub - ValXXII/Browser: Le navigateur"; gh2->title = L"Issues · ValXXII/Browser · GitHub";
        yt->title = L"Accueil - YouTube"; yt->uri = L"https://www.youtube.com/";
        pages = {yt, paper, gh1, gh2, lu}; current = 0; yt->usedAt = ++useClock; ApplyPages();
        auto m = BuildSplitMenu(paper);
        assert(m.items.size() >= 6 && m.items[0].kind == CM::Kind::Header && !m.items[0].enabled);
        assert(m.items[0].label == L"Avec YouTube" && m.items[0].second == L"· affiché" && m.items[0].tag == 1);
        const CM::Item* right = find(m.items, L"À droite"); assert(right && right->glyph == CM::Glyph::Right && right->accessible == L"À droite, Avec YouTube");
        assert(find(m.items, L"À gauche") && find(m.items, L"Au-dessus") && find(m.items, L"En dessous"));
        // other sites: strip order, minus the source and the header's target; homonyms get a second word
        const CM::Item* other = find(m.items, L"Avec un autre site"); assert(other && other->children.size() == 3);
        assert(other->children[0].label == L"GitHub" && other->children[0].second == L"Browser" && other->children[1].label == L"GitHub" && other->children[1].second == L"Issues");
        assert(other->children[2].label == L"Luma" && other->children[2].second.empty() && find(other->children[1].children, L"À droite"));
        // same last path segment: the strip order tells them apart
        gh2->uri = L"https://github.com/someone/Browser"; m = BuildSplitMenu(paper); other = find(m.items, L"Avec un autre site");
        assert(other->children[0].second == L"1" && other->children[1].second == L"2"); gh2->uri = L"https://github.com/ValXXII/Browser/issues";
        // highlighting a row designates its target in the strip (1 DIP outline) and clears after
        SplitMenuHighlight(right); assert(chrome.menuTargets == std::vector<int>({0}));
        SplitMenuHighlight(nullptr); assert(chrome.menuTargets.empty());
        // the clicked tab is the one on screen: no default target, only "Avec un autre site"
        auto self = BuildSplitMenu(yt); assert(self.items.size() == 1 && self.items[0].label == L"Avec un autre site" && self.items[0].children.size() == 4);
        assert(self.items[0].glyph == CM::Glyph::None && find(m.items, L"Avec un autre site")->glyph == CM::Glyph::None);  // flush with the text, never an empty slot
        // "À droite" really splits with the named target; another site works without being on screen
        m = BuildSplitMenu(paper); run(m, find(m.items, L"À droite"), paper);
        assert(paper->group && paper->group == yt->group && TabIndexOf(paper) == TabIndexOf(yt) + 1 && Active() == paper && CurrentSplit().members.size() == 2);
        m = BuildSplitMenu(lu); other = find(m.items, L"Avec un autre site");
        // the group on screen heads the menu; GitHub Issues (off screen) is in the other-site list
        assert(m.items[0].label == L"Avec la vue partagée" && m.items[0].second == L"· 2 sites" && m.items[0].icons.size() == 2 && m.items[0].tag == -paper->group);
        assert(find(m.items, L"Ajouter au début") && find(m.items, L"Ajouter à la fin") && find(m.items, L"Insérer entre") && find(m.items, L"Remplacer un site"));
        assert(!find(m.items, L"Au-dessus"));  // a side-by-side group has no top / bottom rows
        assert(find(m.items, L"Insérer entre")->children.size() == 1 && find(m.items, L"Insérer entre")->children[0].label == L"Entre YouTube et Paper");
        run(m, find(m.items, L"Ajouter à la fin"), lu); assert(lu->group == yt->group && CurrentSplit().members.size() == 3 && CurrentSplit().members[2] == lu);
        // a full group: start / end / between unavailable, with the reason row; replacing stays
        m = BuildSplitMenu(gh1);
        const CM::Item* start = find(m.items, L"Ajouter au début"); assert(start && !start->enabled && start->accessible.find(L"indisponible") != std::wstring::npos);
        const CM::Item* note = find(m.items, L"Pas de place pour un 4ᵉ site · 360 px minimum"); assert(note && note->kind == CM::Kind::Note && !note->enabled);
        assert(!find(m.items, L"Insérer entre")->enabled);
        assert(note->glyph == CM::Glyph::None && find(m.items, L"Insérer entre")->glyph == CM::Glyph::None && find(m.items, L"Remplacer un site")->glyph == CM::Glyph::None);  // flush: only positions carry a glyph
        const CM::Item* replace = find(m.items, L"Remplacer un site"); assert(replace && replace->enabled && replace->children.size() == 3 && replace->children[1].label == L"Paper");
        run(m, &replace->children[1], gh1);
        assert(gh1->group == yt->group && !paper->group && TabIndexOf(paper) >= 0 && CurrentSplit().members.size() == 3);  // replaced, never closed
        // into a group that is not on screen: Select another tab, then add to the group from the list
        Select(TabIndexOf(paper)); assert(!VisibleGroup());
        m = BuildSplitMenu(gh2); other = find(m.items, L"Avec un autre site"); const CM::Item* shared = nullptr;
        for (auto& c : other->children) if (c.label == L"Vue partagée") shared = &c;
        assert(shared && shared->second == L"· 3 sites" && !find(shared->children, L"Ajouter à la fin")->enabled);
        run(m, find(shared->children, L"Remplacer un site")->children.data(), gh2);  // replaces YouTube
        assert(gh2->group && gh2->group == gh1->group && !yt->group && Active() == gh2 && VisibleGroup() == gh2->group);
        for (auto& t : pages) t->group = 0; splitGroups.clear();
        pages = {yt, luma, paper}; current = 0; yt->title = L"Youtube"; yt->uri = L"https://Youtube.test/"; ApplyPages();
    }

    // 14. Rounded page corners: the outer ones, single page and split alike (#127, Val: the four corners in every layout).
    {
        splitGroups.clear(); for (auto& t : pages) t->group = 0; current = 0; ApplyPages();
        auto cs = PageCorners(); { int tops = 0; for (auto& c : cs) tops += c.top; assert(cs.size() == 4 && tops == 2 && !cs[0].right && cs[1].right); }
        SV::Target t; t.kind = SV::Kind::Split; t.side = SV::Side::Right; t.orientation = SV::Orientation::Row;
        ApplySplitTarget(pages[1], t, pages[0], CurrentSplit()); auto v = CurrentSplit(); cs = PageCorners();
        assert(v.members.size() == 2 && cs.size() == 4 && cs[0].page.left == v.a.slots[0].box.left && !cs[0].right && cs[2].page.right == v.a.slots[1].box.right && cs[2].right);
        assert(chrome.split.activeBottomLeft != chrome.split.activeBottomRight);  // the ring rounds only the active panel's outer corner
        splitGroups[v.group] = SV::Orientation::Column; ApplyPages(); v = CurrentSplit(); cs = PageCorners();
        assert(cs.size() == 4); for (auto& c : cs) assert(c.page.top == v.a.slots[c.top ? 0 : 1].box.top);  // stacked: the top panel's top, the bottom panel's bottom
        QuitGroup(v.group);
    }

    // 15. A page element in fullscreen inside a split: presented alone at the whole ContentBox; no other panel, grip,
    //     rail, separator hit or F6 panel. The group (members, order, orientation, weights) is untouched and comes
    //     back exactly. F11 alone keeps the split; F11 + page fullscreen returns to the F11 split.
    {
        splitGroups.clear(); for (auto& t : pages) t->group = 0; current = 0; ApplyPages();
        SV::Target t; t.kind = SV::Kind::Split; t.side = SV::Side::Right; t.orientation = SV::Orientation::Row;
        ApplySplitTarget(pages[1], t, pages[0], CurrentSplit());
        auto v0 = CurrentSplit(); assert(v0.group && v0.members.size() == 2 && chrome.split.shown && chrome.split.handles.size() == 1);
        v0.members[0]->weight = 1.6f; v0.members[1]->weight = .4f; ApplyPages(); v0 = CurrentSplit();
        const auto a = v0.members[v0.active], b = v0.members[v0.active == 0 ? 1 : 0];
        const auto members = v0.members; const auto weights = v0.weights; const float wa = a->weight, wb = b->weight; const auto boxes = v0.a.slots; const auto orient = splitGroups[v0.group];
        const auto order = pages; assert(a == Active() && a->shown && b->shown && ShownPanels().size() == 2);
        auto same = [&](const SplitView& v) { if (v.group != v0.group || v.members != members || v.weights != weights || v.a.slots.size() != boxes.size()) return false;
            for (size_t i = 0; i < boxes.size(); ++i) { const auto& p = v.a.slots[i].box; const auto& q = boxes[i].box; if (p.left != q.left || p.top != q.top || p.right != q.right || p.bottom != q.bottom || v.a.slots[i].member != boxes[i].member) return false; }
            return true; };
        auto alone = [&](const char* what) {
            const auto v = CurrentSplit(); (void)what;
            assert(!v.group && a->shown && !b->shown && Shown() == 1);
            assert(!chrome.split.shown && chrome.split.handles.empty() && chrome.split.rails.empty() && splitGrips.empty() && ShownPanels().empty());
            assert(SeparatorAt((boxes[0].box.right + boxes[1].box.left) / 2, 400, v) < 0);   // the former gap is no separator
            assert(a->group == v0.group && b->group == v0.group && splitGroups[v0.group] == orient && pages == order && a->weight == wa && b->weight == wb && wa != wb);
        };
        // Page fullscreen (windowed): the requesting active page alone, at the ContentBox.
        a->pageFullscreen = true; fullscreen.page = true; ApplyPages(); SyncSplitDecor(); alone("page");
        // Leaving: the exact split again (same members, order, weights and boxes).
        a->pageFullscreen = false; fullscreen.page = false; ApplyPages(); SyncSplitDecor();
        assert(same(CurrentSplit()) && a->shown && b->shown && chrome.split.shown && chrome.split.handles.size() == 1 && ShownPanels().size() == 2);
        // F11 alone keeps the split.
        fullscreen.browser = true; ApplyPages(); SyncSplitDecor(); assert(CurrentSplit().group == v0.group && a->shown && b->shown);
        // F11 + page fullscreen: alone; page leaves: the F11 split; F11 leaves: the windowed split.
        a->pageFullscreen = true; fullscreen.page = true; ApplyPages(); SyncSplitDecor(); alone("nested");
        a->pageFullscreen = false; SetPageFullscreen(a, false);   // the engine's « left fullscreen », F11 still on: the host re-lays out
        assert(!fullscreen.page && fullscreen.browser && CurrentSplit().group == v0.group && a->shown && b->shown && chrome.split.shown);
        fullscreen.browser = false; ApplyPages(); SyncSplitDecor(); assert(same(CurrentSplit()) && Shown() == 2);
        // Only the page that asked: the flag on the hidden (non-active) panel never collapses the split.
        b->pageFullscreen = true; fullscreen.page = true; ApplyPages(); assert(CurrentSplit().group == v0.group && Shown() == 2);
        b->pageFullscreen = false; fullscreen.page = false; ApplyPages();
        QuitGroup(v0.group);
    }

    // 16. Mode Focus (design/focus-mode-plan.md, Paper 1J9P-0): the page 4 DIP inside the window on all four sides, its
    //     four corners round (6 DIP), the chrome a floating panel that starts hidden; fullscreen still wins; split pages
    //     get their top corners too. Layout()'s order without its EDIT (no real field in this fixture).
    {
        auto relayout = [] { SyncFocusMode(); BuildChrome(); FocusPlace(); ApplyPages(); };
        splitGroups.clear(); for (auto& t : pages) t->group = 0; current = 0; ApplyPages();
        settings.focusMode = true; relayout();
        assert(chrome.floating && chrome.ClientWidth() == 1180 && chrome.width == 1170);   // the chrome's layout width: client - 10
        { auto c = ContentBox(); assert(c.left == 4 && c.top == 4 && c.right == 1176 && c.bottom == 811); }
        assert(PageTopDip() == 4 && ChromeBottomDip() == 88 && NativeChrome::Downloads::DrawerLayout::liveTop == 92);
        // Hidden at first: nothing of the chrome is hit or on screen; the tab row's space over the page is no caption.
        assert(!chrome.PanelShown() && !focusBar.Shown() && chrome.Hit(1150, 20) == 0 && chrome.Hit(60, 25) == 0);
        assert(WndProc(window, WM_NCHITTEST, 0, MAKELPARAM(805, 25)) == HTCLIENT);
        // Four round corners (6 DIP) on a single page.
        { auto cs = PageCorners(); int tops = 0; for (auto& c : cs) tops += c.top; assert(cs.size() == 4 && tops == 2 && PageRadiusDip() == 6); }
        // Revealed (the keyboard: at once): the chrome lives in the panel, translated by (5, 5).
        FocusReveal(); assert(focusBar.Shown() && chrome.PanelShown() && focusPlaced == 1);
        assert(chrome.panel.left == 4 && chrome.panel.top == 4 && chrome.panel.right == 1176 && chrome.panel.bottom == 88 && chrome.origin.x == 5 && chrome.origin.y == 5);
        assert(chrome.Hit(1150, 20) == 'X' && chrome.Hit(1172, 20) == 0);   // the close button at x 1132 .. 1164 chrome-local: 1137 .. 1169 on screen
        assert(chrome.Hit(5 + 50, 5 + 20) == NativeChrome::TabBase && chrome.Hit(3, 20) == 0 && chrome.Hit(600, 90) == 0);
        { RECT r = ChromePx(chrome.bar); assert(r.left == 5 + (LONG)chrome.bar.left && r.top == 49 && r.bottom == 81); }   // the field at 44 .. 76 in the panel: 49 .. 81
        assert(WndProc(window, WM_NCHITTEST, 0, MAKELPARAM(805, 25)) == HTCAPTION);   // the tab row's empty space drags the window
        { auto cs = PageCorners(); int tops = 0; for (auto& c : cs) tops += c.top; assert(cs.size() == 4 && tops == 2); }   // corners kept; the top masks step away (SyncCornerMasks)
        // Split side by side: the outer panels get their top corners too; the ring follows the active one's.
        SV::Target t; t.kind = SV::Kind::Split; t.side = SV::Side::Right; t.orientation = SV::Orientation::Row;
        ApplySplitTarget(pages[1], t, pages[0], CurrentSplit()); auto v = CurrentSplit();
        { auto cs = PageCorners(); int tops = 0; for (auto& c : cs) tops += c.top; assert(v.members.size() == 2 && cs.size() == 4 && tops == 2);
          for (auto& c : cs) if (c.top) assert(c.page.top == 4 && (c.right ? c.page.right == 1176 : c.page.left == 4)); }
        assert(chrome.split.activeTopLeft != chrome.split.activeTopRight && chrome.split.activeBottomLeft != chrome.split.activeBottomRight);
        for (auto& s : v.a.slots) assert(s.box.top == 4 && s.box.bottom == 811);
        splitGroups[v.group] = SV::Orientation::Column; ApplyPages(); v = CurrentSplit();
        { auto cs = PageCorners(); assert(cs.size() == 4); for (auto& c : cs) assert(c.top ? c.page.top == 4 : c.page.bottom == 811); }   // stacked: top panel's top, bottom panel's bottom
        QuitGroup(v.group); ApplyPages();
        // Code review: a surface anchored to the chrome opened from the keyboard (Ctrl+Maj+M's profile menu, a region's
        // popover or menu) brings the panel at rest first, so its anchor is on screen.
        {   const double t0 = FocusMs(NativeChrome::Motion::Now()); focusBar.Reset(false, t0); FocusApply(0); assert(!focusBar.Shown() && !chrome.PanelShown());
            (void)ProfileButtonScreen(); assert(focusBar.Shown() && focusPlaced == 1 && chrome.PanelShown());
            focusBar.Reset(false, t0); FocusApply(0); (void)RegionScreen('K'); assert(focusBar.Shown() && focusPlaced == 1); }
        // Code review: a press on the panel while it slides lands it at rest first; press and release then hit the same
        // control (here the first tab, under the pointer once at rest): a click, not a lost press or a stray drag.
        {   current = 1; ApplyPages(); BuildChrome(); const double t0 = FocusMs(NativeChrome::Motion::Now());
            FM::Input rv; rv.reveal = rv.pointer = true; focusBar.Reset(false, t0); FocusApply(0); focusBar.Update(t0, rv, false); FocusApply(.6f);
            assert(focusBar.Moving(t0 + 1) && chrome.Hit(5 + 100, 25) == (int)'R');
            WndProc(window, WM_LBUTTONDOWN, MK_LBUTTON, At(5 + 100, 25));
            const double t1 = FocusMs(NativeChrome::Motion::Now());
            assert(!focusBar.Moving(t1) && focusBar.Position(t1) == 1 && tabDrag.tab == pages[0] && std::fabs(tabDrag.grabY - (25 - chrome.origin.y - chrome.strip.Surface(0).top)) < .01f);
            WndProc(window, WM_LBUTTONUP, 0, At(5 + 100, 25)); assert(current == 0 && !tabDrag.tab); }
        // Fullscreen wins: the page alone, no panel; focus mode comes back after.
        fullscreen.browser = true; relayout();
        assert(!chrome.floating && !chrome.PanelShown()); { auto c = ContentBox(); assert(c.left == 0 && c.top == 0 && c.right == 1180 && c.bottom == 815); }
        fullscreen.browser = false; relayout(); assert(chrome.floating && !focusBar.Shown()); { auto c = ContentBox(); assert(c.top == 4 && c.bottom == 811); }
        // Off again: the normal chrome exactly as before (#127: inset 4, 81 DIP, the four corners, radius 6).
        settings.focusMode = false; relayout();
        assert(!chrome.floating && chrome.width == 1180 && chrome.origin.x == 0 && NativeChrome::Downloads::DrawerLayout::liveTop == 81);
        { auto c = ContentBox(); assert(c.left == 4 && c.top == 81 && c.right == 1176 && c.bottom == 811); auto cs = PageCorners(); int tops = 0; for (auto& k : cs) tops += k.top; assert(cs.size() == 4 && tops == 2 && PageRadiusDip() == 6); }
        assert(chrome.Hit(1172, 20) == 'X');
    }

    // 17. Mode Focus with the annotation mode (owner's rule 2026-11-24; Paper « Annotation + Focus · barre du haut » 203J-0,
    //     « … panneau révélé » 20MM-0, « … tiroir ouvert » 21OK-0): the banner stays at the very top, Focus hides only the
    //     tabs and the toolbar. This fixture is unstamped (no annotation mode compiled: src/main.cpp): the banner's height is
    //     set the way the mode sets it (chrome.annotationBand); the banner's and the chat card's own placement (Ann::BandRect,
    //     Chat::PanelRect) are checked in a real window (tests/focus/annotation_focus_journey.py), their geometry in
    //     tests/focus_mode_test.cpp.
    {
        namespace DL = NativeChrome::Downloads::DrawerLayout;
        auto relayout = [] { SyncFocusMode(); FocusAbove(); BuildChrome(); FocusPlace(); ApplyPages(); };
        splitGroups.clear(); for (auto& t : pages) t->group = 0; current = 0;
        settings.focusMode = true; chrome.annotationBand = 32; relayout();
        assert(chrome.floating);   // the annotation mode no longer suspends focus mode
        assert(chrome.panelAbove.banner == 32 && chrome.panelAbove.reserve == 0 && chrome.width == 1170);
        { auto c = ContentBox(); assert(c.left == 4 && c.top == 32 && c.right == 1176 && c.bottom == 811); }   // under the banner (Mode Commentaire: no card)
        { auto k = FocusChatCard(380); assert(k.left == 796 && k.top == 32 && k.right == 1176 && k.bottom == 811); }   // 203J-0: the card 380 wide, 4 from the edges
        assert(PageTopDip() == 32 && ChromeBottomDip() == 120 && DL::liveTop == 124 && DL::liveRight == 4 && DL::liveBottom == 4 && DL::liveRadius == 6);   // 21OK-0
        { auto cs = PageCorners(); int tops = 0; for (auto& c : cs) { tops += c.top; if (c.top) assert(c.page.top == 32); } assert(cs.size() == 4 && tops == 2); }
        // Hidden: the panel waits behind the banner (nothing of it on screen, nothing hit).
        assert(!chrome.PanelShown() && chrome.Hit(60, 20) == 0 && chrome.Hit(60, 40) == 0);
        {   const double t0 = FocusMs(NativeChrome::Motion::Now()); focusBar.Reset(false, t0); FocusApply(.3f);
            assert(chrome.panel.top < 32 && chrome.PanelVisible().top == 32 && chrome.Hit(60, 20) == 0); }   // mid-slide: only its part under the banner
        // Revealed (20MM-0): top 36, 4 under the banner; the chrome inside at (5, 37); the banner's strip never hit.
        FocusReveal(); assert(focusBar.Shown() && chrome.PanelShown());
        assert(chrome.panel.left == 4 && chrome.panel.top == 36 && chrome.panel.right == 1176 && chrome.panel.bottom == 120 && chrome.origin.x == 5 && chrome.origin.y == 37);
        assert(chrome.Hit(5 + 50, 37 + 20) == NativeChrome::TabBase && chrome.Hit(400, 20) == 0);
        { RECT r = ChromePx(chrome.bar); assert(r.top == 37 + 44 && r.bottom == 37 + 76); }   // the address field follows the panel under the banner
        // With a chat card's strip (Panneau mode: reserve 384) the panel spans the page's width only (788 in 20MM-0).
        chrome.SetPanelAbove({32, FM::ChatReserve(380)}); chrome.PlacePanel(1);
        assert(chrome.width == 1180 - 10 - 384 && chrome.panel.right == 792 && chrome.Hit(800, 60) == 0);
        // The selection paused (the banner goes up, the chat may stay): back to the 4 DIP inset.
        chrome.annotationBand = 0; relayout(); FocusReveal();
        { auto c = ContentBox(); assert(c.top == 4); } assert(chrome.panel.top == 4 && DL::liveTop == 92 && PageTopDip() == 4);
        // Focus off with the annotation mode on: the normal chrome, the banner's height back under the toolbar.
        chrome.annotationBand = 32; settings.focusMode = false; relayout();
        assert(!chrome.floating && chrome.width == 1180 && chrome.panelAbove.banner == 0);
        { auto c = ContentBox(); assert(c.top == 81 + 32 && c.left == 4); }
        assert(DL::liveTop == 81 && DL::liveRight == 0 && DL::liveBottom == 0 && DL::liveRadius == 0);
        chrome.annotationBand = 0; relayout();
    }

    // Text fit: the refusal pill (« Pas de place pour deux sites · 256 px de haut minimum chacun ») is drawn beside
    // the pointer; dragged near the right or bottom edge of the window it must stay whole inside it (8 DIP margin),
    // never cut by the window edge, in French and English, at 96 and 120 DPI.
    {   int pillFailures = 0; wchar_t cwd[MAX_PATH]{}; GetCurrentDirectoryW(MAX_PATH, cwd);
        const std::wstring evidence = std::wstring(cwd) + L"\\build\\text-fit-evidence"; CreateDirectoryW((std::wstring(cwd) + L"\\build").c_str(), nullptr); CreateDirectoryW(evidence.c_str(), nullptr);
        for (auto lang : {I18n::Locale::Fr, I18n::Locale::En}) {
            I18n::SetForProcess(lang);
            for (UINT dpi : {96u, 120u}) for (float width : {560.f, 700.f, 300.f}) {
                const float height = 420, k = dpi / 96.f;
                NativeChrome::Chrome off; assert(SUCCEEDED(off.InitOffscreen(cwd, dpi, width, height)));
                NativeChrome::SplitUI::Painter painter; painter.write = off.WriteFactory(); painter.fonts = off.FontCollection();
                for (int needed : {2, 3}) for (auto o : {SV::Orientation::Row, SV::Orientation::Column}) for (float px : {width * .3f, width - 20}) for (float py : {200.f, height - 10}) {
                    NativeChrome::SplitUI::Frame f; f.pill.shown = true; f.pill.x = px + 14; f.pill.y = py + 22; f.pill.title = RefusalTitle(needed); f.pill.detail = RefusalDetail(o);
                    auto* t = off.RenderTarget(); t->BeginDraw(); t->Clear(D2D1::ColorF(0, 0, 0, 1)); painter.Draw(t, f); assert(SUCCEEDED(t->EndDraw()));
                    const int W = (int)std::lround(width * k), H = (int)std::lround(height * k), m = (int)std::floor(8 * k);
                    bool inside = true, drawn = false;
                    auto lit = [&](int x, int y) { return (off.SurfacePixel((x + .5f) / k, (y + .5f) / k) & 0xFFFFFF) != 0; };
                    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                        const bool band = x < m || y < m || x >= W - m || y >= H - m;
                        if (band) { if (lit(x, y)) inside = false; }
                        else if (!drawn && x % 3 == 0 && y % 3 == 0 && lit(x, y)) drawn = true;
                    }
                    if (!drawn || !inside) std::fprintf(stderr, "refusal pill %s: %ls / %ls, %u dpi, window %g, pointer %g,%g\n", drawn ? "crosses the window edge" : "not drawn", f.pill.title.c_str(), f.pill.detail.c_str(), dpi, width, px, py);
                    if (!(drawn && inside)) ++pillFailures;
                    if (needed == 2 && o == SV::Orientation::Column && px == width - 20 && py == 200.f)
                        off.SavePng((evidence + L"\\split-refusal-pill-" + (lang == I18n::Locale::En ? L"en-" : L"fr-") + std::to_wstring(dpi) + L"-w" + std::to_wstring((int)width) + L".png").c_str());
                }
            }
        }
        I18n::SetForProcess(I18n::Locale::Fr);
        std::printf("refusal pill: %d of 192 frames cross the window edge\n", pillFailures);
        assert(pillFailures == 0);
    }

    pages.clear(); DestroyWindow(window); window = nullptr;
    std::puts("PASS: reorder vs lift, split right, Escape restore, insert, refusal, replace keeps tabs, rails on shrink + reopen, keyboard and pointer resize, close hand-over, menu stacking, reverse, F6 panels, page fullscreen alone in a split, session groups, single tab, menu V2 (target header, other sites, homonyms, full group reason, replace, off-screen group), rounded page corners");
}
