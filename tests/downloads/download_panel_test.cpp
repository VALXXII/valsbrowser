// Download history: model checks + offscreen renders of the popover states.
// Usage: download_panel_test.exe <repo root> <output folder>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include "../../src/download-panel.h"

using namespace NativeChrome;
using namespace NativeChrome::Downloads;
static int failures = 0;
static void Expect(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; } }

// Engine state → row (the bug: a download the engine holds after its last byte stayed « 100 % » forever).
static void HeldChecks() {
    using namespace Engine;
    Expect(MapEngineState(Completed, 0, false) == State::Completed && MapEngineState(InProgress, 0, false) == State::InProgress && MapEngineState(InProgress, 0, true) == State::Paused, "engine states: completed, in progress, paused by the user");
    Expect(MapEngineState(Interrupted, ReasonUserPaused, false) == State::Paused && MapEngineState(Interrupted, ReasonUserCanceled, false) == State::Cancelled, "engine interrupts: paused, cancelled");
    Expect(MapEngineState(Interrupted, 10, false) == State::Failed && MapEngineState(Interrupted, 1, false) == State::Failed, "engine interrupts: anything else fails (blocked reasons keep the B07 path)");
    const std::int64_t mb = 57430695;
    Expect(!HeldForDecision(InProgress, mb, mb, false, -1) && !HeldForDecision(InProgress, mb, mb, false, HoldGraceMs - 1), "every byte in: not held before the grace");
    Expect(HeldForDecision(InProgress, mb, mb, false, HoldGraceMs) && HeldForDecision(InProgress, mb, mb, false, 240000), "every byte in, still in progress past the grace: held (measured: no event ever follows)");
    Expect(!HeldForDecision(Completed, mb, mb, false, 240000) && !HeldForDecision(Interrupted, mb, mb, false, 240000), "a completed or interrupted download is never held");
    Expect(!HeldForDecision(InProgress, mb - 1, mb, false, 240000) && !HeldForDecision(InProgress, 5, 0, false, 240000) && !HeldForDecision(InProgress, mb, mb, true, 240000), "missing bytes, unknown size or a user pause: never held");
    Item held; held.state = State::InProgress; held.held = true;
    Expect(ActionsFor(held).first == Action::Discard && ActionsFor(held).second == Action::Keep, "held actions: Supprimer, Conserver");
    Expect(std::wcscmp(IconFor(Action::Discard), L"trash") == 0 && std::wcscmp(IconFor(Action::Keep), L"check") == 0, "held actions' drawer glyphs");
    // Popover: the card takes its row's place, 20 from neighbouring tiles; « View all » 16 under the last element.
    std::vector<Item> list(3); list[1].state = State::InProgress; list[1].held = true;
    const float card = HeldCard::Height(72);
    auto slots = DownloadsUI::PopoverSlots(list, card);
    Expect(slots.size() == 4 && slots[0].height == 0 && slots[1].height == card && slots[1].top == 16 + 32 + 20 && slots[2].top == slots[1].top + card + 20 && slots[3].top == slots[2].top + 32 + 16, "popover: card spacing");
    Expect(DownloadsUI::PopoverHeight(slots) == slots[3].top + 16 + 16, "popover height follows the card");
    auto plain = DownloadsUI::PopoverSlots(list);
    Expect(plain[1].height == 0 && plain[1].top == 64, "without a measured card the held row is a plain row");
    Expect(HeldCard::Button(0, card, 0).right + HeldCard::buttonGap == HeldCard::Button(0, card, 1).left && HeldCard::Button(0, card, 1).right == HeldCard::right - HeldCard::inset && HeldCard::Button(0, card, 0).bottom == card - HeldCard::inset, "card buttons: two equal halves, 12 inside");
}

static void ModelChecks() {
    Expect(Bytes(52848230, 209715200) == L"50.4/200MB", "bytes in total's unit");
    Expect(Bytes(512, 2048) == L"0.5/2KB", "sub-unit received");
    Expect(Bytes(3 * 1024 * 1024, 0) == L"3MB", "unknown total");
    Expect(Bytes(1536LL * 1024 * 1024, 4LL * 1024 * 1024 * 1024) == L"1.5/4GB", "gigabytes");
    Expect(Remaining(14) == L"00:14" && Remaining(723) == L"12:03" && Remaining(3723) == L"1:02:03" && Remaining(-1).empty(), "remaining time");
    Expect(FileName(L"C:\\Users\\a\\Downloads\\orca-windows-setup.exe") == L"orca-windows-setup.exe", "file name");
    Expect(FolderLabel(L"C:\\Users\\a\\Downloads\\orca-windows-setup.exe") == L"In \u2026/Downloads", "folder label");
    Expect(FolderLabel(L"D:\\file.zip") == L"In D:", "drive root");
    Expect(ParseIsoTime(L"1970-01-01T00:00:14Z") == 14000, "iso epoch");
    Expect(ParseIsoTime(L"2024-05-01T12:00:00.250Z") == 1714564800250LL, "iso fraction");
    Expect(ParseIsoTime(L"2024-05-01T14:00:00+02:00") == 1714564800000LL, "iso offset");
    Expect(ParseIsoTime(L"") == 0 && ParseIsoTime(L"soon") == 0, "iso invalid");
    // History round trip: only finished rows, unicode intact, malformed lines skipped.
    std::vector<Item> items(4);
    items[0].state = State::InProgress; items[0].path = L"C:\\d\\partial.bin";
    items[1].state = State::Completed; items[1].path = L"C:\\d\\Caf\u00e9 \U0001F600.pdf"; items[1].total = 1234; items[1].uri = L"https://x.test/a";
    items[2].state = State::Failed; items[2].path = L"C:\\d\\b.zip"; items[2].received = 10; items[2].reason = L"Network error";
    items[3].state = State::Cancelled; items[3].path = L"C:\\d\\c.iso";
    std::stringstream io; WriteHistory(io, items);
    std::string text = io.str() + "Z\t1\tbad\turi\nC\tx\tC:\\bad\t\nC\t5\t\xff\xfe\t\n";
    std::istringstream in(text); int next = 7; auto read = ReadHistory(in, next);
    Expect(read.size() == 3, "history keeps finished rows only");
    Expect(read.size() == 3 && read[0].path == items[1].path && read[0].total == 1234 && read[0].uri == items[1].uri && read[0].id == 7, "history completed row");
    Expect(read.size() == 3 && read[1].state == State::Failed && read[1].reason == L"Network error" && read[2].state == State::Cancelled, "history states");
    Expect(next == 10, "history ids continue");
    std::istringstream bad("other 2\nC\t1\tC:\\x\t\n"); next = 1; Expect(ReadHistory(bad, next).empty(), "unknown version ignored");
    // Actions per state follow the Paper frame.
    Item item; item.state = State::InProgress; Expect(ActionsFor(item).first == Action::Pause && ActionsFor(item).second == Action::Cancel, "in-progress actions");
    item.state = State::Completed; Expect(ActionsFor(item).first == Action::ShowInFolder && ActionsFor(item).second == Action::Open, "completed actions");
    item.missing = true; Expect(ActionsFor(item).first == Action::None && ActionsFor(item).second == Action::Remove, "deleted file actions");
    // A row click opens only a finished download whose file is still there.
    {   Item r; r.state = State::Completed; const bool done = DownloadsUI::Openable(r); r.missing = true; const bool gone = DownloadsUI::Openable(r); r.missing = false;
        bool others = false; for (auto st : {State::InProgress, State::Paused, State::Failed, State::Cancelled}) { r.state = st; others = others || DownloadsUI::Openable(r); }
        Expect(done && !gone && !others, "row click opens only a finished download whose file is still there"); }
    Expect(DownloadsUI::IsRow(DownloadsUI::RowHit(7)) && !DownloadsUI::IsRow(DownloadsUI::ActionHit(7, 1)) && DownloadsUI::HitItem(DownloadsUI::RowHit(7)) == 7 && DownloadsUI::HitItem(DownloadsUI::ActionHit(7, 1)) == 7, "row and action targets of a row");
    // Reveal: staggered entrance, faster joint exit, continuous on interruption.
    RowReveal reveal; const auto t0 = Motion::Time{} + std::chrono::seconds(10);
    reveal.Set(true, t0, false);
    Expect(reveal.Layer(0, t0).opacity == 0 && reveal.Layer(0, t0).blur == Reveal::maxBlur && reveal.Layer(0, t0).offset == Reveal::maskTravel, "reveal starts hidden, blurred, offset");
    const auto mid = t0 + Motion::Milliseconds(60);
    Expect(reveal.Layer(0, mid).opacity > reveal.Layer(1, mid).opacity && reveal.Layer(1, mid).opacity > reveal.Layer(2, mid).opacity, "mask leads, actions follow");
    Expect(reveal.NextWake(mid).frames, "reveal requests frames");
    const auto settled = t0 + Reveal::enter + Reveal::stagger * 2;
    Expect(reveal.Layer(2, settled).opacity == 1 && reveal.Layer(2, settled).blur == 0 && reveal.Layer(2, settled).offset == 0, "reveal settles crisp");
    Expect(!reveal.NextWake(settled).delay, "settled reveal is idle");
    const auto before = reveal.Layer(1, mid).opacity; reveal.Set(false, mid, false);
    Expect(reveal.Layer(1, mid).opacity == before, "exit continues from current value");
    Expect(reveal.Layer(0, mid + Reveal::exit).opacity == 0 && reveal.Layer(2, mid + Reveal::exit).opacity == 0, "exit completes together");
    RowReveal reduced; reduced.Set(true, t0, true);
    Expect(reduced.Layer(2, t0 + Motion::Milliseconds(50)).offset == 0 && reduced.Layer(2, t0 + Motion::Milliseconds(50)).blur == 0, "reduced motion: fade only");
    // Geometry from the Paper frame.
    Expect(Panel::MaskSolid() == 186 && Panel::MaskStart() == 98, "mask spans the name before the actions");
    Expect(Panel::Close().left == 232 && Panel::Close().right == 256, "section carets on the close-button column of the drawers' header (none drawn here)");
    // Periods: local calendar days (offset +1h here), unknown start = oldest.
    const std::int64_t day = 86400000LL, hour = 3600000LL, noon = 20000 * day + 11 * hour; // local 12:00
    Expect(PeriodOf(noon - 11 * hour, noon, hour) == Period::Today, "same local day");
    Expect(PeriodOf(noon - 13 * hour, noon, hour) == Period::Yesterday, "previous local day");
    Expect(PeriodOf(noon - 7 * day, noon, hour) == Period::LastWeek && PeriodOf(noon - 8 * day, noon, hour) == Period::LastMonth, "week boundary");
    Expect(PeriodOf(noon - 31 * day, noon, hour) == Period::LastMonth && PeriodOf(noon - 40 * day, noon, hour) == Period::LastYear, "month boundary");
    Expect(PeriodOf(0, noon, hour) == Period::LastYear, "unknown start");
    // History v2 keeps the start time; v1 files still load.
    std::vector<Item> dated(1); dated[0].state = State::Completed; dated[0].path = L"C:\\d\\a.zip"; dated[0].total = 9; dated[0].started = 1714564800000LL;
    std::stringstream v2; WriteHistory(v2, dated); int id2 = 1; auto back = ReadHistory(v2, id2);
    Expect(back.size() == 1 && back[0].started == 1714564800000LL && back[0].path == dated[0].path, "history v2 start time");
    std::istringstream v1("browser-downloads 1\r\nC\t9\tC:\\d\\old.zip\thttps://x.test\t\n"); int id1 = 1; auto old = ReadHistory(v1, id1);
    Expect(old.size() == 1 && old[0].started == 0 && old[0].path == L"C:\\d\\old.zip", "history v1 compatible");
    // Curves and layout.
    Expect(Curves::drawer.At(0) == 0 && Curves::drawer.At(1) == 1 && Curves::drawer.At(.25f) > .5f, "drawer curve starts fast");
    // Morph spring: from rest (first 120 Hz frame moves < 2%), monotonic, lands exactly.
    {
        const auto& m = DrawerMotion::morphSpring; const float frame = 1000.f / 120 / DrawerMotion::morph.count();
        bool monotonic = true; float last = 0; for (int i = 1; i <= 200; ++i) { const float v = m.At(i / 200.f); monotonic = monotonic && v >= last && v <= 1.0001f; last = v; }
        Expect(m.At(0) == 0 && m.At(frame) < .02f && monotonic && m.At(1) == 1 && std::abs(m.At(.999f) - 1) < .002f, "morph spring starts from rest and settles without overshoot");
        Expect(DrawerMotion::morph.count() >= 400 && DrawerMotion::morph.count() <= 520, "morph settle time");
    }
    Expect(std::abs(Curves::emphasized.At(.5f) - Motion::Eased(.5f, Motion::Curve::Icon)) < .002f, "bezier matches the existing icon curve");
    Expect(Panel::Height(4, true) == 240 && Panel::Height(9, true) == 240 && Panel::Height(0) == 64, "popover: four rows + View all, no header");
    std::vector<Item> six(6); auto pop = DownloadsUI::PopoverSlots(six);
    Expect(pop.size() == 5 && pop.back().kind == DownloadsUI::SlotKind::ViewAll && pop.back().top == 208 && pop[0].top == 16, "popover slots: the first row on the top padding");
    std::array<std::vector<int>, PeriodCount> groups; groups[0] = {0, 1, 2, 3}; groups[1] = {4, 5};
    std::array<float, PeriodCount> open{1, 1, 1, 1, 1}; float bottom = 0;
    auto slots = DownloadsUI::DrawerSlots(six, groups, open, 0, &bottom);
    Expect(slots.size() == 8 && slots[0].top == 12 && slots[5].kind == DownloadsUI::SlotKind::Section && slots[5].top == 252, "drawer sections on the 240 DIP stride, the first label on the top padding");
    Expect(slots[1].top == 48 && bottom == 252 + 24 + 12 + 2 * 48 + 16, "drawer rows and content end");
    open[0] = 0; auto folded = DownloadsUI::DrawerSlots(six, groups, open, 0);
    Expect(folded.size() == 4 && folded[1].kind == DownloadsUI::SlotKind::Section && folded[1].top == 12 + 36, "folded section keeps only its header");
    open[0] = .5f; auto half = DownloadsUI::DrawerSlots(six, groups, open, 0);
    auto start = DownloadsUI::MorphSlots(six, slots, 0), end = DownloadsUI::MorphSlots(six, slots, 1);
    Expect(start[1].top == 16 && start[0].opacity == 0 && start.back().kind == DownloadsUI::SlotKind::ViewAll && start.back().opacity == 1, "morph starts as the popover");
    Expect(end[1].top == 48 && end[0].opacity == 1 && end.back().opacity == 0 && end[5].opacity == 1, "morph lands on the drawer");
    Expect(half[1].kind == DownloadsUI::SlotKind::Row && half[1].clipBottom == 12 + 24 + .5f * (12 + 4 * 48) && half[1].opacity == .5f, "folding rows are clipped and faded");
    HeldChecks();
}

struct Fixture {
    std::wstring root, out;
    std::vector<Item> items;
    Fixture(std::wstring r, std::wstring o) : root(std::move(r)), out(std::move(o)) {
        Item a; a.id = 1; a.path = L"C:\\Users\\a\\Downloads\\orca-windows-setup.exe"; a.state = State::InProgress; a.received = 52848230; a.total = 209715200;
        Item b = a; b.id = 2;
        Item c; c.id = 3; c.path = a.path; c.state = State::Completed; c.received = c.total = 209715200;
        Item d = c; d.id = 4;
        items = {a, b, c, d};
    }
    // hovered: per row id, time since hover began (ms); -1 = settled.
    // shape: optional visible rectangle (the window region); outside it shows a page grey.
    bool Paint(const wchar_t* name, UINT dpi, float height, std::vector<Item>& rows, std::map<int, int> hovered, const std::vector<DownloadsUI::Slot>& slots, float entrance, int focus, DownloadsUI::Scene scene, float width = Panel::width, const D2D1_RECT_F* shape = nullptr) {
        Chrome chrome;
        if (FAILED(chrome.InitOffscreen(root, dpi, width, height))) { std::fprintf(stderr, "offscreen init failed\n"); return false; }
        DownloadsUI::View view(chrome);
        std::map<int, RowReveal> reveals; const auto now = Motion::Time{} + std::chrono::seconds(100);
        for (auto [id, ms] : hovered) { auto& r = reveals[id]; if (ms < 0) r.Snap(true); else r.Set(true, now - Motion::Milliseconds(ms), false); }
        scene.items = &rows; scene.reveals = &reveals; scene.now = now; scene.entrance = entrance; scene.focus = focus; scene.keyboard = focus != 0;
        scene.nowMs = 1000000; for (auto& item : rows) if (item.state == State::InProgress) item.endTime = scene.nowMs + 14000;
        auto* target = chrome.RenderTarget(); target->BeginDraw(); view.Paint(scene, slots, height);
        if (shape) {
            chrome.Color(40 / 255.f);
            target->FillRectangle(D2D1::RectF(0, 0, width, shape->top), chrome.SolidBrush());
            target->FillRectangle(D2D1::RectF(0, shape->bottom, width, height), chrome.SolidBrush());
            target->FillRectangle(D2D1::RectF(0, shape->top, shape->left, shape->bottom), chrome.SolidBrush());
            target->FillRectangle(D2D1::RectF(shape->right, shape->top, width, shape->bottom), chrome.SolidBrush());
        }
        HRESULT hr = target->EndDraw();
        view.Release();
        if (FAILED(hr)) { std::fprintf(stderr, "EndDraw %08lx\n", static_cast<unsigned long>(hr)); return false; }
        return SUCCEEDED(chrome.SavePng((out + L"\\" + name).c_str()));
    }
    bool Render(const wchar_t* name, UINT dpi, std::map<int, int> hovered, float entrance = 1, int focus = 0, std::vector<Item>* list = nullptr) {
        auto& rows = list ? *list : items;
        const int shown = std::min<int>(Panel::popoverRows, static_cast<int>(rows.size()));
        const float height = Panel::Height(shown, !rows.empty());
        DownloadsUI::Scene scene; scene.viewBottom = height - Panel::padding / 2;
        return Paint(name, dpi, height, rows, hovered, DownloadsUI::PopoverSlots(rows), entrance, focus, scene);
    }
    // Popover with a held download's card (Paper B07b « A · Ambre »), height measured like the app.
    bool Held(const wchar_t* name, UINT dpi, std::vector<Item>& rows, int focus = 0, std::map<int, int> hovered = {}) {
        float card = 0;
        { Chrome measure; if (FAILED(measure.InitOffscreen(root, dpi, Panel::width, 200))) return false; DownloadsUI::View v(measure); card = v.CardHeight(); v.Release(); measure.Dispose(); }
        if (card <= 0) { std::fprintf(stderr, "held card not measured\n"); return false; }
        auto slots = DownloadsUI::PopoverSlots(rows, card); const float height = DownloadsUI::PopoverHeight(slots);
        DownloadsUI::Scene scene; scene.viewBottom = height - Panel::padding / 2;
        std::printf("held card: %.0f DIP high, popover %.0f DIP\n", card, height);
        return Paint(name, dpi, height, rows, hovered, slots, 1, focus, scene);
    }
    bool HeldDrawer(const wchar_t* name, UINT dpi, std::vector<Item>& rows) {
        std::array<std::vector<int>, PeriodCount> groups; for (size_t i = 0; i < rows.size(); ++i) groups[0].push_back(static_cast<int>(i));
        auto slots = DownloadsUI::DrawerSlots(rows, groups, {1, 1, 1, 1, 1}, 0);
        DownloadsUI::Scene scene; scene.viewTop = DrawerLayout::viewTop; scene.viewBottom = 200; scene.edge = true;
        return Paint(name, dpi, 200, rows, {}, slots, 1, 0, scene);
    }
    // Drawer: three periods holding the four Paper rows; `open` per period.
    bool Drawer(const wchar_t* name, UINT dpi, float height, std::array<float, PeriodCount> open, float scroll, int focus = 0) {
        std::vector<Item> rows; std::array<std::vector<int>, PeriodCount> groups;
        for (int p = 0; p < 3; ++p) for (int k = 0; k < 4; ++k) { Item item = items[k]; item.id = p * 10 + k + 1; groups[p].push_back(static_cast<int>(rows.size())); rows.push_back(item); }
        float bottom = 0; auto slots = DownloadsUI::DrawerSlots(rows, groups, open, scroll, &bottom);
        DownloadsUI::Scene scene; scene.viewTop = DrawerLayout::viewTop; scene.viewBottom = height; scene.edge = true;
        scene.fadeTop = Motion::Clamp(scroll / DrawerLayout::topFade); scene.fadeBottom = Motion::Clamp((bottom - height - scroll) / Panel::pitch);
        return Paint(name, dpi, height, rows, {{2, -1}, {4, -1}, {12, -1}, {14, -1}, {22, -1}}, slots, 1, focus, scene);
    }
    // Popover -> drawer morph at progress t (eased), as the drawer window paints it.
    bool Morph(const wchar_t* name, UINT dpi, float height, float t) {
        std::vector<Item> rows; std::array<std::vector<int>, PeriodCount> groups;
        for (int p = 0; p < 3; ++p) for (int k = 0; k < 4; ++k) { Item item = items[k]; item.id = p * 10 + k + 1; groups[p].push_back(static_cast<int>(rows.size())); rows.push_back(item); }
        float bottom = 0; auto docked = DownloadsUI::DrawerSlots(rows, groups, {1, 1, 1, 1, 1}, 0, &bottom);
        const float width = Panel::width + DrawerLayout::margin, popoverHeight = Panel::Height(4, true);
        DownloadsUI::Scene scene; scene.viewTop = DrawerLayout::viewTop; scene.edge = true; scene.edgeOpacity = t;
        scene.originX = DrawerLayout::margin * t; // popover column starts at x 0 (6 DIP in from the window edge)
        scene.viewBottom = height + (popoverHeight - Panel::padding / 2 - height) * (1 - t);
        scene.fadeBottom = Motion::Clamp((bottom - height) / Panel::pitch) * t;
        const float edge = 4 * t * (1 - t); scene.fadeBottom = std::max(scene.fadeBottom, edge); scene.fadeTop = edge; scene.fadeTopSize = DrawerLayout::topFade * 2; scene.viewTop = Panel::ViewTop() + (DrawerLayout::viewTop - Panel::ViewTop()) * t;
        const D2D1_RECT_F shape{DrawerLayout::margin * t, 0, Panel::width + (width - Panel::width) * t, popoverHeight + (height - popoverHeight) * t};
        return Paint(name, dpi, height, rows, {}, DownloadsUI::MorphSlots(rows, docked, t), 1, 0, scene, width, &shape);
    }
};

// ---- Toolbar icon model (download-icon.h) ----
static void IconChecks() {
    using namespace NativeChrome::DownloadIcon;
    const auto t0 = Motion::Time{} + std::chrono::seconds(50);
    auto at = [&](double ms) { return t0 + std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double, std::milli>(ms)); };
    const double frame = 1000.0 / Fps;
    Expect(RingFrames == 82 && ArrowFrames == 120, "icon frame layout matches the generator");
    Model m; Expect(m.At(t0).idle && !m.NextWake(t0).delay, "idle: static icon, no frames");
    m.Update(true, 0, false, t0, false);
    Expect(!m.At(t0).idle && m.At(t0).ring == 0 && m.At(t0).arrow == 0, "start leaves from the tray pose");
    Expect(m.At(at(7 * frame + 1)).ring == 7 && m.At(at(7 * frame + 1)).arrow == 7, "start morph plays in time");
    Expect(m.At(at(20 * frame)).ring == Morph, "waiting: grey ring, no fill");
    const int a1 = m.At(at(20 * frame)).arrow, a2 = m.At(at(40 * frame)).arrow;
    Expect(a1 > Morph && a2 > a1 && a2 < LoopEnd, "arrow loops on its own clock");
    m.Update(true, .5f, false, at(40 * frame), false);
    Expect(m.At(at(41 * frame)).ring == Morph + Steps / 2 && m.At(at(41 * frame)).arrow != m.At(at(55 * frame)).arrow, "fill follows progress while the arrow keeps moving");
    // Completion at frame 60: the loop restarts at 14 + 78 = 92 -> done waits for that boundary.
    m.Update(false, 1, true, at(60 * frame), false);
    Expect(m.At(at(61 * frame)).ripple >= 0 && m.At(at(61 * frame)).ring == FillEnd, "completed: full ring + ripple right away");
    // Success (user request): from the loop boundary (92) the arrow morphs into the check (24 frames),
    // holds 3 s with the ring full, morphs back, then the usual done (ring unrolls, arrow rises).
    Expect(m.At(at(91 * frame)).ring == FillEnd && m.At(at(91 * frame)).success < 0, "success waits for the loop boundary (arrow at rest)");
    Expect(m.At(at(92 * frame + 1)).success == 0 && m.At(at(92 * frame + 1)).ring == FillEnd, "morph starts from the arrow at rest, ring held full");
    Expect(m.At(at(98 * frame + 1)).success == 6 && m.At(at(104 * frame + 1)).success == 12 && m.At(at(110 * frame + 1)).success == 18, "arrow -> check morph plays in time (quarter, half, three quarters)");
    const double hold = 116 * frame;   // 92 + 24
    Expect(m.At(at(hold + 1)).success == SuccessMorph, "check reached after 400 ms");
    Expect(m.At(at(hold + 2999)).success == SuccessMorph && m.At(at(hold + 2999)).ring == FillEnd, "check holds for 3 s");
    Expect(m.NextWake(at(hold + 100)).delay && m.NextWake(at(hold + 100)).delay->count() > 2800, "holding: one wake at the end of the 3 s, no frames");
    Expect(m.At(at(hold + 3000 + 12 * frame + 1)).success == 12, "check -> arrow morph back after the hold");
    const double back = hold + 3000 + 24 * frame;
    Expect(m.At(at(back + 1)).success < 0 && m.At(at(back + 2 * frame)).ring > FillEnd && m.At(at(back + 2 * frame)).arrow > LoopEnd, "then the usual done (ring unrolls, arrow rises)");
    m.Settle(at(back + 30 * frame));
    Expect(m.At(at(back + 30 * frame)).idle && !m.Active() && !m.NextWake(at(back + 30 * frame)).delay, "back to the static icon, no stale success, no more frames");
    // Cancel: the start, reversed, without ripple.
    Model c; c.Update(true, .3f, false, t0, false); c.Update(false, .3f, false, at(30 * frame), false);
    Expect(c.At(at(31 * frame)).ripple < 0, "cancel: no ripple");
    { bool any = false; for (int f = 30; f < 140; ++f) any = any || c.At(at(f * frame)).success >= 0; Expect(!any, "cancel / failure: never the success check"); }
    Expect(c.At(at(93 * frame)).ring < Morph && c.At(at(93 * frame)).ring > 0, "cancel: ring rolls back into the tray");
    // A new download while leaving starts right after.
    // A new download during the success: the check morphs back from where it is (frame 8 at 100),
    // then progress resumes straight from the arrow at rest (no tray morph).
    Model q; q.Update(true, 0, false, t0, false); q.Update(false, 1, true, at(20 * frame), false);
    Expect(q.At(at(100 * frame + 1)).success == 8, "success morph under way");
    q.Update(true, .25f, false, at(100 * frame + 1), false);
    Expect(q.At(at(104 * frame + 2)).success == 4, "interrupted: the check morphs back from where it was");
    q.Settle(at(109 * frame + 2));
    Expect(q.Active() && !q.At(at(109 * frame + 2)).idle && q.At(at(109 * frame + 2)).success < 0 && q.At(at(109 * frame + 2)).ring == Morph + Steps / 4, "then progress for the new download");
    // Interrupted during the hold: back from the full check.
    Model h; h.Update(true, 0, false, t0, false); h.Update(false, 1, true, at(20 * frame), false);
    h.Update(true, 0, false, at(116 * frame + 1500), false);
    Expect(h.At(at(116 * frame + 1500 + 1)).success == SuccessMorph && h.At(at(116 * frame + 1500 + 12 * frame + 1)).success == 12, "new download during the hold: morph back from the check at once");
    // Reduced motion: the final check, static, for 3 s; then idle. A new download replaces it at once.
    Model rs; rs.Update(true, .5f, false, t0, true); rs.Update(false, 1, true, at(100), true);
    Expect(rs.At(at(101)).success == SuccessMorph && rs.At(at(3099)).success == SuccessMorph, "reduced motion: static check for 3 s (no morph frames)");
    rs.Settle(at(3101)); Expect(rs.At(at(3101)).idle && !rs.Active(), "reduced motion: idle after 3 s");
    Model ri; ri.Update(true, .5f, false, t0, true); ri.Update(false, 1, true, at(100), true); ri.Update(true, .1f, false, at(500), true);
    Expect(ri.At(at(501)).success < 0 && !ri.At(at(501)).idle, "reduced motion: a new download replaces the check at once");
    // Reduced motion: no morph, no loop, no ripple; progress still shown.
    Model r; r.Update(true, .5f, false, t0, true);
    Expect(r.At(t0).ring == Morph + Steps / 2 && r.At(at(500)).arrow == Morph && !r.NextWake(t0).delay, "reduced motion: static ring with progress");
    // The x4 ripple stays inside the repainted rectangle (16 DIP icon, button centre).
    {
        const float reachDip = Ripple::reach * 16 / 256, ringDip = (Ripple::radius * Ripple::grow + Ripple::stroke * Ripple::grow / 2) * 16 / 256;
        Expect(Ripple::grow == 4 && reachDip > ringDip && reachDip < 40, "ripple reach covers the x4 ring and its blur");
    }
    // Composition: grey ink like the other icons, fill opaque, the cut-out removes both.
    const uint32_t opaque = 0xFF949494u, none = 0, blue = 0xFF3E67FFu;
    std::vector<uint32_t> out;
    Compose(&opaque, &none, &none, &none, 1, out); Expect((out[0] >> 24) == 128 && (out[0] & 255) == 128, "track: white at 50%");
    Compose(&opaque, &blue, &none, &none, 1, out); Expect(out[0] == blue, "fill covers the track");
    const uint32_t cut = 0xFFFFFFFFu;
    Compose(&opaque, &blue, &cut, &none, 1, out); Expect(out[0] == 0, "cut-out is transparent");
    // Profile accent (Paper p-I-0 P07): the fill takes the window's ink from its own coverage; the
    // grey track/arrow and the cut-out are untouched; half coverage stays half (antialiasing kept).
    const float green[3] = {0x6F / 255.f, 0xD0 / 255.f, 0x87 / 255.f};
    Compose(&opaque, &blue, &none, &none, 1, out, green); Expect(out[0] == 0xFF6FD087u, "tinted fill: profile ink (#6FD087), opaque");
    const uint32_t halfBlue = 0x801F3480u;   // premultiplied #3E67FF at 50 %
    Compose(&none, &halfBlue, &none, &none, 1, out, green); Expect((out[0] >> 24) == 128 && ((out[0] >> 8) & 255) == 104, "tinted edge keeps its coverage (premultiplied)");
    Compose(&opaque, &none, &none, &none, 1, out, green); Expect((out[0] >> 24) == 128 && (out[0] & 255) == 128, "track stays the grey ink under a tint");
    Compose(&opaque, &blue, &cut, &none, 1, out, green); Expect(out[0] == 0, "cut-out stays transparent under a tint");
    // Success check (arrow layer) in the profile ink: opaque at full mix, the grey ink at 0, half way in between.
    const uint32_t arrowPx = 0xFFFFFFFFu;
    Compose(&none, &none, &none, &arrowPx, 1, out, green, 1); Expect(out[0] == 0xFF6FD087u, "success check: profile ink, opaque");
    Compose(&none, &none, &none, &arrowPx, 1, out, green, 0); Expect((out[0] >> 24) == 128 && (out[0] & 255) == 128, "mix 0: the usual grey arrow");
    Compose(&none, &none, &none, &arrowPx, 1, out, green, .5f); Expect((out[0] >> 24) == 191 && ((out[0] >> 8) & 255) == 168, "mix .5: half way grey -> ink (premultiplied)");
    const float orange[3] = {0xFB / 255.f, 0x95 / 255.f, 0x4A / 255.f};
    Compose(&opaque, &blue, &none, &arrowPx, 1, out, orange, 1); Expect(out[0] == 0xFFFB954Au, "another profile ink (orange) over the full ring");
}

// ---- active downloads: always-visible Pause / Cancel, UIA, pointer lanes, keyboard (user report 2026-11-14) ----
static int CancelSurfaceChecks(const std::wstring& root) {
    using namespace NativeChrome; using namespace NativeChrome::Downloads; namespace UI = NativeChrome::DownloadsUI;
    int failures = 0; auto expect = [&](bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; };
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DownloadCancelTestOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, -12000, -12000, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
    std::vector<Item> items(4);
    items[0].id = 1; items[0].state = State::InProgress; items[0].received = 400000; items[0].total = 4000000; items[0].path = L"C:\\d\\a-very-long-download-file-name-that-goes-under-the-actions-mask (2).bin";
    items[1].id = 2; items[1].state = State::Paused; items[1].received = 1000; items[1].total = 4000; items[1].path = L"C:\\d\\paused.zip";
    items[2].id = 3; items[2].state = State::Completed; items[2].path = L"C:\\d\\done.pdf";
    items[3].id = 4; items[3].state = State::Cancelled; items[3].path = L"C:\\d\\stopped.bin";
    std::vector<std::pair<int, Action>> fired;
    UI::Host::Hooks hooks; hooks.schedule = [](Motion::Time) {}; hooks.action = [&](int id, Action a) { fired.push_back({id, a}); };
    for (int surface = 0; surface < 2; ++surface) {
        const char* name = surface == 0 ? "popover" : "drawer";
        UI::Popover pop; UI::Drawer drawer; UI::Host* host = surface == 0 ? static_cast<UI::Host*>(&pop) : static_cast<UI::Host*>(&drawer);
        const bool made = surface == 0 ? pop.Create(wc.hInstance, owner, root, &items, hooks) : drawer.Create(wc.hInstance, owner, root, &items, hooks);
        expect(made, surface == 0 ? "popover created" : "drawer created");
        if (!made) continue;
        host->ShowForTest(Panel::width + (surface ? DrawerLayout::margin : 0), surface ? 728 : Panel::Height(4, true));
        // 1. Without any pointer: the active rows (in progress, paused) show their actions; the others don't.
        expect(host->RevealShownForTest(1) && host->RevealShownForTest(2) && !host->RevealShownForTest(3) && !host->RevealShownForTest(4),
               surface == 0 ? "popover: Pause/Cancel visible at rest on active rows only (no hover needed)" : "drawer: Pause/Cancel visible at rest on active rows only");
        // 2. Accessible tree: each row a ListItem with its status, its actions Buttons (Invoke) inside the surface.
        const auto nodes = host->AccessibleNodes(); RECT w{}; GetWindowRect(host->Window(), &w);
        auto find = [&](const wchar_t* id, int item) -> const Accessibility::Node* { for (const auto& n : nodes) if (n.automationId == id && n.key == UI::ActionHit(item, 0) + (std::wstring(id) == L"DownloadCancel" ? 1 : 0)) return &n; return nullptr; };
        const auto* cancel1 = find(L"DownloadCancel", 1); const auto* pause1 = find(L"DownloadPause", 1); const auto* cancel2 = find(L"DownloadCancel", 2); const auto* resume2 = find(L"DownloadResume", 2);
        int rows = 0, cancelled4 = 0; bool statusOk = false;
        for (const auto& n : nodes) { if (n.automationId == L"DownloadItem") { ++rows; if (n.key == UI::Host::RowKeyBaseForTest() + 1 && n.status == Bytes(items[0].received, items[0].total) && !n.status.empty()) statusOk = true; } if (n.parent == UI::Host::RowKeyBaseForTest() + 4 && n.automationId == L"DownloadCancel") ++cancelled4; }
        expect(rows == 4 && statusOk, "UIA: four ListItems, the active one says its progress");
        expect(cancel1 && pause1 && cancel2 && resume2 && cancel1->role == Accessibility::Role::Button && cancel1->name == L"Cancel download" && cancel1->parent == UI::Host::RowKeyBaseForTest() + 1,
               "UIA: Cancel download / Pause / Resume are Buttons under their row");
        expect(cancelled4 == 0, "UIA: a cancelled row offers no Cancel (only Remove)");
        if (cancel1 && pause1 && cancel2) {
            const RECT c = cancel1->bounds, pz = pause1->bounds;
            expect(c.left >= w.left && c.right <= w.right && c.right - c.left >= 20 && pz.right <= c.left && c.left == cancel2->bounds.left && c.right == cancel2->bounds.right,
                   "Cancel lane: inside the surface, >= 20 px, right of Pause, aligned across rows (long names stay under the mask)");
        }
        // 3. Pointer: the Cancel lane cancels THAT row; the name area does nothing (no accidental cancel / open).
        const float k = GetDpiForWindow(host->Window()) / 96.f;
        auto click = [&](float x, float y) { const LPARAM lp = MAKELPARAM(static_cast<int>(x * k), static_cast<int>(y * k)); SendMessageW(host->Window(), WM_MOUSEMOVE, 0, lp); SendMessageW(host->Window(), WM_LBUTTONDOWN, MK_LBUTTON, lp); SendMessageW(host->Window(), WM_LBUTTONUP, 0, lp); };
        if (cancel1 && pause1) {
            const float cx = (cancel1->bounds.left + cancel1->bounds.right) / 2.f - w.left, cy = (cancel1->bounds.top + cancel1->bounds.bottom) / 2.f - w.top;
            fired.clear(); click(cx / k, cy / k);
            expect(fired.size() == 1 && fired[0].first == 1 && fired[0].second == Action::Cancel, "pointer: a click in the Cancel lane cancels that download");
            fired.clear(); click(Panel::textLeft + 20, cy / k);
            expect(fired.empty(), "pointer: a click on the row's name does nothing (no accidental cancel / open)");
            fired.clear(); click(((pause1->bounds.left + pause1->bounds.right) / 2.f - w.left) / k, cy / k);
            expect(fired.size() == 1 && fired[0].second == Action::Pause, "pointer: the Pause lane pauses, never cancels");
        }
        // 4. Keyboard: no close button any more: Tab walks Pause -> Cancel; Entrée activates Cancel.
        if (surface == 0) {
            fired.clear(); for (int i = 0; i < 2; ++i) SendMessageW(host->Window(), WM_KEYDOWN, VK_TAB, 0);
            SendMessageW(host->Window(), WM_KEYDOWN, VK_RETURN, 0);
            expect(fired.size() == 1 && fired[0].first == 1 && fired[0].second == Action::Cancel, "keyboard: Tab x2 + Entrée cancels the active download");
        }
        // 4b. No header: no close button in the tree, the focus order or under the pointer at its former place.
        {   bool closeNode = false; for (const auto& n : nodes) closeNode = closeNode || n.automationId == L"DownloadsClose" || n.name == TR(common_close);
            const auto order = host->FocusOrderForTest(); bool small = false; for (int hit : order) small = small || hit < UI::ViewAllHit;
            expect(!closeNode && !small, surface == 0 ? "popover: no close button (UIA, focus order)" : "drawer: no close button (UIA, focus order)");
            bool list = false; for (const auto& n : nodes) list = list || (n.role == Accessibility::Role::List && n.name == TR(downloads_history_title));
            expect(list, "UIA: the list keeps « Download history » as its name (not drawn)"); }
        // 4c. A finished row opens its file: a click anywhere on it but its buttons, Entrée on the focused row,
        //     UIA Invoke on its ListItem; all through the same hook as its Open action. Its buttons fire once.
        {   const Accessibility::Node* done = nullptr; const Accessibility::Node* paused = nullptr; const Accessibility::Node* stopped = nullptr; const Accessibility::Node* folder = nullptr; const Accessibility::Node* open3 = nullptr;
            for (const auto& n : nodes) {
                if (n.key == UI::Host::RowKeyBaseForTest() + 3) done = &n;
                if (n.key == UI::Host::RowKeyBaseForTest() + 2) paused = &n;
                if (n.key == UI::Host::RowKeyBaseForTest() + 4) stopped = &n;
                if (n.key == UI::ActionHit(3, 0)) folder = &n;
                if (n.key == UI::ActionHit(3, 1)) open3 = &n;
            }
            expect(done && paused && stopped && folder && open3 && done->focusable && !paused->focusable && !stopped->focusable, "UIA: a finished row is focusable (Invoke opens it); other rows are not");
            auto midY = [&](const Accessibility::Node* n) { return ((n->bounds.top + n->bounds.bottom) / 2.f - w.top) / k; };
            auto midX = [&](const Accessibility::Node* n) { return ((n->bounds.left + n->bounds.right) / 2.f - w.left) / k; };
            if (done && paused && stopped && folder && open3) {
                fired.clear(); click(Panel::textLeft + 20, midY(done));
                expect(fired.size() == 1 && fired[0].first == 3 && fired[0].second == Action::Open, "pointer: a click on a finished row's name opens the file (the Open hook)");
                fired.clear(); click(Panel::padding + 10, midY(done));
                expect(fired.size() == 1 && fired[0].second == Action::Open, "pointer: a click on a finished row's icon opens the file too");
                fired.clear(); click(midX(folder), midY(folder));
                expect(fired.size() == 1 && fired[0].first == 3 && fired[0].second == Action::ShowInFolder, "pointer: the folder button shows the folder, once, without opening the file");
                fired.clear(); click(midX(open3), midY(open3));
                expect(fired.size() == 1 && fired[0].first == 3 && fired[0].second == Action::Open, "pointer: the Open button opens once (no double fire with the row)");
                fired.clear(); click(Panel::textLeft + 20, midY(paused)); click(Panel::textLeft + 20, midY(stopped));
                expect(fired.empty(), "pointer: a paused or cancelled row's name does nothing");
                // Entrée on the focused row.
                const auto order = host->FocusOrderForTest(); const auto at = std::find(order.begin(), order.end(), UI::RowHit(3));
                bool rows = true; for (int id : {1, 2, 4}) rows = rows && std::find(order.begin(), order.end(), UI::RowHit(id)) == order.end();
                expect(at != order.end() && at + 1 != order.end() && *(at + 1) == UI::ActionHit(3, 0) && rows, "focus order: a finished row comes before its buttons; other rows are not stops");
                fired.clear(); for (size_t i = 0; i < order.size() && host->FocusForTest() != UI::RowHit(3); ++i) SendMessageW(host->Window(), WM_KEYDOWN, VK_TAB, 0);
                SendMessageW(host->Window(), WM_KEYDOWN, VK_RETURN, 0);
                expect(host->FocusForTest() == UI::RowHit(3) && fired.size() == 1 && fired[0].first == 3 && fired[0].second == Action::Open, "keyboard: Entrée on the focused finished row opens the file");
                fired.clear(); host->InvokeNodeForTest(UI::Host::RowKeyBaseForTest() + 3);
                expect(fired.size() == 1 && fired[0].first == 3 && fired[0].second == Action::Open, "UIA Invoke on a finished row opens the file");
                fired.clear(); host->InvokeNodeForTest(UI::Host::RowKeyBaseForTest() + 4); host->InvokeNodeForTest(UI::Host::RowKeyBaseForTest() + 1);
                expect(fired.empty(), "UIA Invoke on a cancelled or active row does nothing");
                items[2].missing = true; host->ModelChangedForTest(); fired.clear(); click(Panel::textLeft + 20, midY(done));
                expect(fired.empty(), "pointer: a finished row whose file is gone does nothing"); items[2].missing = false; host->ModelChangedForTest();
            }
        }
        // 5. UI Automation Invoke reaches the same action.
        fired.clear(); SendMessageW(host->Window(), UI::Host::InvokeMessageForTest(), static_cast<WPARAM>(UI::ActionHit(2, 1)), 0);
        expect(fired.size() == 1 && fired[0].first == 2 && fired[0].second == Action::Cancel, "UIA Invoke: Cancel of the paused download");
        fired.clear(); SendMessageW(host->Window(), UI::Host::InvokeMessageForTest(), static_cast<WPARAM>(UI::ActionHit(4, 1)), 0);
        bool cancelFired = false; for (auto& f : fired) if (f.second == Action::Cancel) cancelFired = true;
        expect(!cancelFired, "UIA Invoke: no Cancel on a cancelled row (its slot is Remove: disabled for Cancel)");
        // 6. A row that stops being active loses its permanent actions (Cancel gone after the engine says Canceled).
        SendMessageW(host->Window(), WM_MOUSELEAVE, 0, 0);   // pointer gone, keyboard focus on the finished row: only the model decides
        SendMessageW(host->Window(), UI::Host::InvokeMessageForTest(), static_cast<WPARAM>(UI::RowHit(3)), 1);
        items[0].state = State::Cancelled; host->ModelChangedForTest();
        expect(!host->RevealShownForTest(1), surface == 0 ? "after cancel: the row's actions hide (no hover)" : "drawer: after cancel the actions hide");
        items[0].state = State::InProgress;
        (void)name; host->Dispose(); DestroyWindow(host->Window());
    }
    DestroyWindow(owner);
    return failures;
}

// ---- "View all" morph: nothing from before shows at the growing edges (user report 2026-11-23: small flashes) ----
// The drawer is a colour-keyed layered window (NativeUI::KeyedDrawer): DWM shows its surface through its window region,
// the key colour transparent, and a present only reaches the pixels inside the region in force when it presents. So the
// surface is first filled with a colour the drawer never draws (what an earlier session leaves there), then the real morph
// runs frame by frame (off screen, DWM-cloaked, never activated) and, after the hand-off and after every frame, the surface
// is read through the region: no pixel DWM may show can still hold that colour. The region must also stay the same for the
// whole morph (one present per frame; it only drops the margin strip once landed), and the first and last frames show
// exactly the shapes the region used to cut (the popover's, the docked column's).
namespace MorphFlash {
constexpr uint32_t stale = 0x01FE03, key = 0xFF00FF;   // 0xRRGGBB as a 32-bit DIB holds them
struct Shot { int w = 0, h = 0; std::vector<uint32_t> px; std::vector<uint8_t> inRegion; HRGN region = nullptr; };
static bool Read(HWND hwnd, Shot& s) {
    RECT c{}; GetClientRect(hwnd, &c); s.w = c.right; s.h = c.bottom; if (s.w <= 0 || s.h <= 0) return false;
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = s.w; bi.bmiHeader.biHeight = -s.h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr; HDC dc = GetDC(hwnd); HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { DeleteDC(mem); ReleaseDC(hwnd, dc); return false; }
    HGDIOBJ old = SelectObject(mem, bmp); auto* p = static_cast<uint32_t*>(bits); const size_t n = static_cast<size_t>(s.w) * s.h;
    BitBlt(mem, 0, 0, s.w, s.h, dc, 0, 0, SRCCOPY); GdiFlush(); s.px.assign(p, p + n);
    if (s.region) DeleteObject(s.region);
    s.region = CreateRectRgn(0, 0, 0, 0); if (GetWindowRgn(hwnd, s.region) == ERROR) { DeleteObject(s.region); s.region = CreateRectRgn(0, 0, s.w, s.h); }
    PatBlt(mem, 0, 0, s.w, s.h, BLACKNESS); FillRgn(mem, s.region, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))); GdiFlush();
    s.inRegion.resize(n); for (size_t i = 0; i < n; ++i) s.inRegion[i] = (p[i] & 0xFFFFFF) != 0;
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(hwnd, dc); return true;
}
static bool Shown(const Shot& s, size_t i) { return s.inRegion[i] && (s.px[i] & 0xFFFFFF) != key; }
static int StaleShown(const Shot& s) { int n = 0; for (size_t i = 0; i < s.px.size(); ++i) if (Shown(s, i) && (s.px[i] & 0xFFFFFF) == stale) ++n; return n; }
// Pixels where "shown by DWM" differs from `expected` (the region the morph used to cut, window pixels).
static int Mismatch(const Shot& s, HRGN expected) { int n = 0; for (int y = 0; y < s.h; ++y) for (int x = 0; x < s.w; ++x) if (Shown(s, static_cast<size_t>(y) * s.w + x) != (PtInRegion(expected, x, y) != FALSE)) ++n; return n; }
}
static int MorphFlashChecks(const std::wstring& root) {
    using namespace NativeChrome; using namespace NativeChrome::Downloads; namespace UI = NativeChrome::DownloadsUI; using namespace MorphFlash;
    int failures = 0; auto expect = [&](bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; };
    // 1. Pure: the keyed-out silhouette is the GDI region's, pixel for pixel (round-rect shapes as the morph makes them, four scales).
    for (UINT dpi : {96u, 120u, 144u, 192u}) {
        Chrome chrome; if (FAILED(chrome.InitOffscreen(root, dpi, 160, 160))) { expect(false, "keyed silhouette: offscreen chrome"); continue; }
        const int side = static_cast<int>(160 * dpi / 96), radius = static_cast<int>(std::lround(Panel::radius * dpi / 96.f));
        int wrong = 0;
        for (float t : {0.f, .13f, .5f, .87f}) {
            auto lerp = [t](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * t)); };
            const int d = static_cast<int>(std::lround(radius * (1 - t))) * 2 + 1;
            HRGN shape = CreateRoundRectRgn(lerp(3, 9), lerp(17, 0), lerp(side - 40, side - 2) + 1, lerp(side / 2, side - 1) + 1, d, d), outside = CreateRectRgn(0, 0, side, side);
            CombineRgn(outside, outside, shape, RGN_DIFF);
            auto* target = chrome.RenderTarget(); target->BeginDraw(); target->Clear(D2D1::ColorF(Design::Surface::canvas, Design::Surface::canvas, Design::Surface::canvas, 1));
            chrome.KeyOutRegion(outside); target->EndDraw();
            for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
                const bool keyed = chrome.SurfacePixel((x + .5f) * 96 / dpi, (y + .5f) * 96 / dpi) == 0xFFFF00FFu;
                if (keyed != (PtInRegion(outside, x, y) != FALSE)) ++wrong;
            }
            DeleteObject(shape); DeleteObject(outside);
        }
        chrome.Dispose();
        std::printf("     keyed silhouette at %u DPI: %d pixel(s) differ from the GDI region\n", dpi, wrong);
        expect(wrong == 0, "keyed silhouette: exactly the GDI round-rect region's pixels (no antialiased key, no gap)");
    }
    // 2. The real drawer and its morph.
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DownloadMorphTestOwner"; RegisterClassW(&wc);
    const int ownerW = 1280, ownerH = 900;
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, -12000, -12000, ownerW, ownerH, nullptr, nullptr, wc.hInstance, nullptr);
    std::vector<Item> items(6);
    for (int i = 0; i < 6; ++i) { items[i].id = i + 1; items[i].state = State::Completed; items[i].path = L"C:\\d\\file-" + std::to_wstring(i + 1) + L".bin"; }
    UI::Host::Hooks hooks; hooks.schedule = [](Motion::Time) {};
    UI::Drawer drawer;
    if (!drawer.Create(wc.hInstance, owner, root, &items, hooks)) { expect(false, "morph: drawer created"); DestroyWindow(owner); return failures; }
    HWND hwnd = drawer.Window(); BOOL cloak = TRUE; DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
    drawer.Warm();   // docked size and place, hidden: as while the popover is open
    // What an earlier session leaves in the surface (never painted by this morph): the stale colour everywhere.
    SetWindowRgn(hwnd, nullptr, FALSE); SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    { RECT c{}; GetClientRect(hwnd, &c); HDC dc = GetDC(hwnd); HBRUSH b = CreateSolidBrush(RGB(0x01, 0xFE, 0x03)); FillRect(dc, &c, b); DeleteObject(b); ReleaseDC(hwnd, dc); }
    DwmFlush(); Shot shot; const bool readable = Read(hwnd, shot) && (shot.px[shot.px.size() / 2] & 0xFFFFFF) == stale;
    ShowWindow(hwnd, SW_HIDE);
    if (!readable) {
        std::printf("skip morph surface checks: this session gives no readable layered-window surface (no desktop composition)\n");
    } else {
        // The popover where ShowDownloads puts it: right edge 6 DIP in from the owner's, top 8 DIP under the toolbar (76 DIP).
        RECT ownerClient{}; GetClientRect(owner, &ownerClient); MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&ownerClient), 2);
        const float k = GetDpiForWindow(owner) / 96.f; auto px = [k](float dip) { return static_cast<int>(std::lround(dip * k)); };
        const RECT from{ownerClient.right - px(DrawerLayout::margin) - px(Panel::width), ownerClient.top + px(76) + px(Panel::anchorGap),
                        ownerClient.right - px(DrawerLayout::margin), ownerClient.top + px(76) + px(Panel::anchorGap) + px(Panel::Height(4, true))};
        drawer.MorphForTest(from);
        RECT w{}; GetWindowRect(hwnd, &w); const int width = w.right - w.left, height = w.bottom - w.top, margin = px(DrawerLayout::margin), r = px(20);
        auto ownerClip = [&] { return CreateRoundRectRgn(-r * 2, -r * 2, width + 1, height + 1, r, r); };
        // Hand-off frame: exactly the popover's shape (the region the morph used to start from), nothing stale.
        Read(hwnd, shot);
        {   const RECT f{from.left - w.left, from.top - w.top, from.right - w.left, from.bottom - w.top}; const int d = px(Panel::radius) * 2 + 1;
            HRGN expected = ownerClip(), shape = CreateRoundRectRgn(f.left, f.top, f.right + 1, f.bottom + 1, d, d); CombineRgn(expected, expected, shape, RGN_AND);
            const int off = Mismatch(shot, expected); std::printf("     hand-off frame: %d pixel(s) off the popover's shape, %d stale\n", off, StaleShown(shot));
            expect(off == 0 && StaleShown(shot) == 0, "morph: the hand-off frame shows exactly the popover's shape, nothing left from before");
            DeleteObject(shape); DeleteObject(expected); }
        // Every frame, driven like the host's frame clock.
        HRGN previous = CreateRectRgn(0, 0, 0, 0); CombineRgn(previous, shot.region, nullptr, RGN_COPY);
        int frames = 0, staleFrames = 0, stalePixels = 0, reshapes = 0;
        while (drawer.MorphingForTest() && frames < 1000) {
            drawer.Frame(Motion::Now()); ++frames;
            if (!Read(hwnd, shot)) break;
            const int n = StaleShown(shot); if (n) { ++staleFrames; stalePixels += n; }
            if (!EqualRgn(previous, shot.region)) { ++reshapes; CombineRgn(previous, shot.region, nullptr, RGN_COPY); }
            Sleep(4);
        }
        DeleteObject(previous);
        std::printf("     morph: %d frames, %d with stale pixels shown (%d pixels), %d region change(s)\n", frames, staleFrames, stalePixels, reshapes);
        expect(frames >= 10 && !drawer.MorphingForTest(), "morph: ran frame by frame to its end");
        expect(staleFrames == 0, "morph: no frame shows what the window held before at its growing edges (the flashes)");
        expect(reshapes <= 1, "morph: the window region stays the same while it grows (one present per frame), it only drops the margin strip once landed");
        // Landed: exactly the docked column (the owner's rounded corner kept), the margin strip gone.
        {   HRGN expected = ownerClip(), column = CreateRectRgn(margin, 0, width, height); CombineRgn(expected, expected, column, RGN_AND);
            const int off = Mismatch(shot, expected); std::printf("     landed: %d pixel(s) off the docked column\n", off);
            expect(off == 0, "morph: lands on exactly the docked column");
            DeleteObject(column); DeleteObject(expected); }
        // Closed mid-morph (Échap): it slides out from where it is; nothing from before shows on the way.
        drawer.Hide(true, false); ShowWindow(hwnd, SW_SHOW);
        { RECT c{}; GetClientRect(hwnd, &c); SetWindowRgn(hwnd, nullptr, FALSE); HDC dc = GetDC(hwnd); HBRUSH b = CreateSolidBrush(RGB(0x01, 0xFE, 0x03)); FillRect(dc, &c, b); DeleteObject(b); ReleaseDC(hwnd, dc); }
        DwmFlush(); ShowWindow(hwnd, SW_HIDE);
        drawer.MorphForTest(from);
        int closing = 0, closingStale = 0;
        for (int i = 0; i < 1000 && drawer.Visible(); ++i) {
            if (i == 5) drawer.Hide(true, true);
            drawer.Frame(Motion::Now()); if (!drawer.Visible() || !Read(hwnd, shot)) break;
            ++closing; if (StaleShown(shot)) ++closingStale;
            Sleep(4);
        }
        std::printf("     closed mid-morph: %d frames, %d with stale pixels shown\n", closing, closingStale);
        expect(closing > 5 && closingStale == 0 && !drawer.Visible() && !IsWindowVisible(hwnd), "morph closed midway (Échap): slides out, nothing from before on the way, then hidden");
    }
    if (shot.region) DeleteObject(shot.region);
    drawer.Dispose(); DestroyWindow(hwnd); DestroyWindow(owner);
    return failures;
}

int wmain(int argc, wchar_t** argv) {
    // This drawer's Paper copy is English: its strings are checked in an English process, set explicitly
    // (never the OS language). The French wording is checked in tests/i18n_test.cpp.
    I18n::SetForProcess(I18n::Locale::En);
    ModelChecks();
    IconChecks();
    if (argc >= 3) {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
        Fixture f(argv[1], argv[2]);
        failures += CancelSurfaceChecks(f.root);
        failures += MorphFlashChecks(f.root);
        // Same scales as the Paper exports: x2.875 (first frame), x1.875 (View all + drawer).
        const UINT paper = 276, drawerPaper = 180;
        Expect(f.Render(L"panel-paper.png", paper, {{2, -1}, {4, -1}}), "render Paper states");
        Expect(f.Render(L"panel-viewall.png", drawerPaper, {{2, -1}, {4, -1}}), "render popover with View all");
        Expect(f.Render(L"panel-viewall-plain.png", drawerPaper, {}), "render popover hand-off reference");
        Expect(f.Render(L"panel-96.png", 96, {{2, -1}, {4, -1}}), "render 100%");
        Expect(f.Render(L"panel-192.png", 192, {{2, -1}, {4, -1}}), "render 200%");
        for (int ms : {0, 40, 80, 120, 160, 220}) {
            wchar_t name[64]; std::swprintf(name, 64, L"reveal-%03d.png", ms);
            Expect(f.Render(name, paper, {{2, ms}}), "render reveal frame");
        }
        Expect(f.Render(L"entrance-050.png", paper, {}, .5f), "render entrance");
        Expect(f.Render(L"focus.png", paper, {{4, -1}}, 1, DownloadsUI::ActionHit(4, 0)), "render keyboard focus");
        Expect(f.Render(L"focus-viewall.png", drawerPaper, {}, 1, DownloadsUI::ViewAllHit), "render View all focus");
        std::vector<Item> states(4);
        states[0].id = 1; states[0].path = L"C:\\d\\video.mp4"; states[0].state = State::Paused; states[0].received = 5000000; states[0].total = 90000000;
        states[1].id = 2; states[1].path = L"C:\\d\\archive.zip"; states[1].state = State::Failed; states[1].reason = L"Network error"; states[1].canResume = true;
        states[2].id = 3; states[2].path = L"C:\\d\\report.pdf"; states[2].state = State::Cancelled;
        states[3].id = 4; states[3].path = L"C:\\d\\gone.txt"; states[3].state = State::Completed; states[3].missing = true;
        Expect(f.Render(L"states.png", paper, {{1, -1}, {2, -1}}, 1, 0, &states), "render other states");
        std::vector<Item> empty; Expect(f.Render(L"empty.png", paper, {}, 1, 0, &empty), "render empty");
        // Drawer at the Paper drawer's height (728 DIP) and scale.
        Expect(f.Drawer(L"drawer-paper.png", drawerPaper, 728, {1, 1, 1, 1, 1}, 0), "render drawer");
        Expect(f.Drawer(L"drawer-96.png", 96, 728, {1, 1, 1, 1, 1}, 0), "render drawer 100%");
        for (int i = 0; i <= 4; ++i) {
            wchar_t name[64]; std::swprintf(name, 64, L"drawer-fold-%d.png", i);
            Expect(f.Drawer(name, drawerPaper, 728, {1, 1 - DrawerMotion::foldSpring.At(i / 4.f), 1, 1, 1}, 0), "render fold frame");
        }
        for (int i = 0; i <= 6; ++i) {
            wchar_t name[64]; std::swprintf(name, 64, L"morph-%d.png", i);
            Expect(f.Morph(name, drawerPaper, 728, DrawerMotion::morphSpring.At(i / 6.f)), "render morph frame");
        }
        Expect(f.Drawer(L"drawer-scrolled.png", drawerPaper, 728, {1, 1, 1, 1, 1}, 120, DownloadsUI::SectionHit(Period::Yesterday)), "render scrolled drawer");
        // Text fit (French and English): the empty state and a blocked download's line fit whole in their room
        // (no ellipsis), measured with the panel's own text stack; French captures at 96 and 120 DPI.
        {
            Chrome measure; Expect(SUCCEEDED(measure.InitOffscreen(f.root, 96, 300, 100)), "text fit: offscreen chrome");
            const float blockedRoom = (Panel::width - Panel::padding) - (Panel::textLeft + Panel::detailIcon + Panel::iconGap), emptyRoom = Panel::width - 2 * Panel::padding;
            for (auto lang : {I18n::Locale::Fr, I18n::Locale::En}) {
                I18n::SetForProcess(lang); const bool en = lang == I18n::Locale::En;
                const float blocked = measure.TextWidth(FailureText(ReasonCode(I18n::Id::download_blocked_check_failed)), Panel::detailText, false, true, -.14f);
                const float empty = measure.TextWidth(TR(downloads_empty), Panel::line, false, false, -.16f);
                std::printf("text fit (%s): blocked line %.1f / %.1f DIP, empty state %.1f / %.1f DIP\n", en ? "en" : "fr", blocked, blockedRoom, empty, emptyRoom);
                Expect(blocked <= blockedRoom, en ? "text fit (en): the blocked download line fits whole" : "text fit (fr): the blocked download line fits whole");
                Expect(empty <= emptyRoom, en ? "text fit (en): the empty state fits whole" : "text fit (fr): the empty state fits whole");
            }
            measure.Dispose();
            I18n::SetForProcess(I18n::Locale::Fr);
            std::vector<Item> fr = f.items; fr[1].state = State::Failed; fr[1].reason = ReasonCode(I18n::Id::download_blocked_check_failed); fr[1].path = L"C:\\Users\\a\\Downloads\\outil-inconnu.exe";
            std::vector<Item> none;
            for (UINT dpi : {96u, 120u}) {
                const std::wstring d = std::to_wstring(dpi);
                Expect(f.Render((L"fr-popover-" + d + L".png").c_str(), dpi, {{4, -1}}, 1, 0, &fr), "render French popover");
                Expect(f.Render((L"fr-empty-" + d + L".png").c_str(), dpi, {}, 1, 0, &none), "render French empty popover");
                Expect(f.Drawer((L"fr-drawer-" + d + L".png").c_str(), dpi, 728, {1, 1, 1, 1, 1}, 0), "render French drawer");
            }
            I18n::SetForProcess(I18n::Locale::En);
        }
        // Held download (B07b): popover card and drawer row, French, 1x / 1.25x / 2x; English at 1x.
        {
            std::vector<Item> held(3);
            held[0].id = 1; held[0].path = L"C:\\Users\\a\\Downloads\\browser-0.3.715-win-x64-setup.exe"; held[0].state = State::InProgress; held[0].held = true; held[0].received = held[0].total = 57430695;
            held[1].id = 2; held[1].path = L"C:\\Users\\a\\Downloads\\Facture_EDF_octobre.pdf"; held[1].state = State::Completed; held[1].received = held[1].total = 217088;
            held[2].id = 3; held[2].path = L"C:\\Users\\a\\Downloads\\HP_M110_Setup_v3.2.1.exe"; held[2].state = State::Failed; held[2].reason = ReasonCode(I18n::Id::download_blocked_dangerous);
            I18n::SetForProcess(I18n::Locale::Fr);
            for (UINT dpi : {96u, 120u, 192u}) {
                const std::wstring d = std::to_wstring(dpi);
                Expect(f.Held((L"held-card-fr-" + d + L".png").c_str(), dpi, held), "render held card");
                Expect(f.HeldDrawer((L"held-drawer-fr-" + d + L".png").c_str(), dpi, held), "render held drawer row");
            }
            Expect(f.Held(L"held-card-fr-focus.png", 192, held, DownloadsUI::ActionHit(1, 1)), "render held card focus");
            I18n::SetForProcess(I18n::Locale::En);
            Expect(f.Held(L"held-card-en-96.png", 96, held), "render held card (en)");
        }
        // Real toolbar (Chrome::Paint) with the animated icon, cropped to the button.
        {
            using namespace NativeChrome::DownloadIcon;
            const auto t0 = Motion::Time{} + std::chrono::seconds(50);
            auto at = [&](double frames) { return t0 + std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double>(frames / Fps)); };
            struct Shot { const wchar_t* name; double when; float fraction; bool complete; };
            const Shot shots[] = {{L"icon-0-idle", -1, 0, false}, {L"icon-1-start", 7, 0, false}, {L"icon-2-waiting", 30, 0, false},
                                  {L"icon-3-bob-low", 14 + 39, 0, false}, {L"icon-4-40pct", 60, .4f, false}, {L"icon-5-ripple", 72, 1, true}, {L"icon-5b-ripple-30", 70 + 11, 1, true}, {L"icon-5c-ripple-60", 70 + 22, 1, true},
                                  {L"icon-6-done", 92 + 12, 1, true}};
            for (const auto& shot : shots) {
                Chrome chrome; if (FAILED(chrome.InitOffscreen(f.root, 180, 360, 84))) { Expect(false, "toolbar offscreen"); continue; }
                Motion::Time now = t0;
                if (shot.when >= 0) {
                    chrome.downloadIcon.Update(true, shot.fraction, false, t0, false);
                    if (shot.fraction > 0) chrome.downloadIcon.Update(true, shot.fraction, false, at(40), false);
                    if (shot.complete) chrome.downloadIcon.Update(false, 1, true, at(70), false);
                    now = at(shot.when);
                }
                std::vector<std::wstring> titles{L"Example"};
                auto* target = chrome.RenderTarget(); (void)target;
                chrome.Paint(titles, 0, L"", false, 0, 0, nullptr, now);
                Expect(SUCCEEDED(chrome.SavePng((f.out + L"\\" + shot.name + L".png").c_str())), "save toolbar shot");
            }
        }
        CoUninitialize();
    }
    if (failures) { std::fprintf(stderr, "%d download panel check(s) failed\n", failures); return 1; }
    std::printf("download panel checks passed\n");
    return 0;
}
