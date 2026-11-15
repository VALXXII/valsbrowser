// Browsing history drawer: model checks (filter, order, periods, labels, layout)
// + offscreen renders of the drawer states.
// Usage: history_panel_test.exe [<repo root> <output folder>]
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <sstream>
#include "../../src/history-panel.h"

using namespace NativeChrome;
using namespace NativeChrome::HistoryView;
static int failures = 0;
static void Expect(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; } }

static const std::int64_t day = 86400000LL, hour = 3600000LL, minute = 60000LL;
static const std::int64_t noon = 20000 * day + 11 * hour; // local 12:00 with a +1h offset (2024-10-04)

static AddressBar::History Sample(std::int64_t now) {
    AddressBar::History h;
    h.Record(L"https://www.github.com/", L"GitHub", now - 2 * hour);
    h.Record(L"https://github.com/VALXXII/browser/issues", L"Issues \u00b7 VALXXII/browser", now - 10 * minute);
    h.Record(L"https://developer.mozilla.org/en-US/docs/Web/CSS", L"CSS: Cascading Style Sheets | MDN", now - day - hour);
    h.Record(L"https://news.ycombinator.com/", L"", now - 3 * day);
    h.Record(L"file:///C:/Users/a/notes.html", L"", now - 20 * day);
    h.Record(L"http://example.com/caf%C3%A9", L"Caf\u00e9 \u00e0 Paris", now - 400 * day);
    return h;
}

static void ModelChecks() {
    const auto history = Sample(noon);
    auto all = Entries(history);
    Expect(all.size() == 6, "every visited page is listed");
    Expect(all.size() == 6 && all[0].url == L"https://github.com/VALXXII/browser/issues" && all[1].url == L"https://www.github.com/", "newest visit first");
    Expect(all.size() == 6 && all[5].url == L"http://example.com/caf%C3%A9", "oldest last");
    // Titles fall back to the readable address; hosts drop "www."; local files say so.
    const auto hn = std::find_if(all.begin(), all.end(), [](const Entry& e) { return e.url == L"https://news.ycombinator.com/"; });
    Expect(hn != all.end() && hn->title == L"news.ycombinator.com" && hn->host == L"news.ycombinator.com" && hn->site == L"news.ycombinator.com", "untitled page shows its address");
    Expect(all[1].host == L"github.com" && all[1].site == L"github.com", "www dropped from host");
    const auto file = std::find_if(all.begin(), all.end(), [](const Entry& e) { return e.url.rfind(L"file:", 0) == 0; });
    Expect(file != all.end() && file->host == L"Local file" && file->local && file->title == L"notes.html" && file->site.empty(), "local file entry reads as its file name");
    // Search: every term, in title or address, case-insensitive; accents kept.
    Expect(Entries(history, L"GIT").size() == 2, "search is case-insensitive over title and address");
    Expect(Entries(history, L"git issues").size() == 1, "every term must match");
    Expect(Entries(history, L"  mdn  ").size() == 1, "query is trimmed");
    Expect(Entries(history, L"caf\u00e9").size() == 1, "non-ASCII titles match");
    Expect(Entries(history, L"ycombinator.com").size() == 1, "address text matches");
    Expect(Entries(history, L"zzz").empty(), "no match");
    Expect(Entries(history, L"").size() == 6 && Entries(history, L"   ").size() == 6, "blank query keeps everything");
    // Periods: local calendar days, like the download drawer.
    const auto groups = Group(all, noon, hour);
    Expect(groups[0].size() == 2 && groups[1].size() == 1 && groups[2].size() == 1 && groups[3].size() == 1 && groups[4].size() == 1, "grouped by period");
    Expect(groups[0].size() == 2 && groups[0][0] == 0 && groups[0][1] == 1, "groups keep newest first");
    // Detail line.
    Expect(ClockText(noon, hour) == L"12:00" && ClockText(noon - 10 * minute, hour) == L"11:50", "local clock");
    Expect(ClockText(0, 0) == L"00:00" && ClockText(-minute, 0) == L"23:59", "clock wraps before the epoch");
    Expect(Detail(all[0], noon, hour) == L"github.com \u00b7 11:50", "today: host and time");
    const auto mdn = std::find_if(all.begin(), all.end(), [](const Entry& e) { return e.host == L"developer.mozilla.org"; });
    Expect(mdn != all.end() && Detail(*mdn, noon, hour) == L"developer.mozilla.org \u00b7 11:00", "yesterday: time");
    const std::int64_t may1 = Downloads::DaysFromCivil(2024, 5, 1) * day + 12 * hour, jan9 = Downloads::DaysFromCivil(2024, 1, 9) * day;
    Expect(DateText(jan9, may1, 0) == L"Jan 9", "same year: month and day");
    Expect(DateText(Downloads::DaysFromCivil(2023, 12, 31) * day, may1, 0) == L"Dec 31, 2023", "other year: with the year");
    Expect(WhenText(jan9, may1, 0) == L"Jan 9" && WhenText(0, may1, 0).empty(), "older visits show the date; unknown nothing");
    for (std::int64_t d : {-1000000LL, -1LL, 0LL, 59LL, 60LL, 365LL, 11016LL, 19844LL, 2932896LL}) {
        const auto c = CivilFromDays(d);
        Expect(Downloads::DaysFromCivil(c.year, c.month, c.day) == d, "civil date round trip");
    }
    Expect(EmptyText(L"") == L"No history yet" && EmptyText(L" rust ") == L"No results for \u201crust\u201d", "empty states");
    // Layout: the download drawer's rhythm under a 32 DIP search field.
    std::array<float, PeriodCount> open{1, 1, 1, 1, 1};
    float bottom = 0; auto slots = Slots(all, groups, open, 0, &bottom);
    Expect(!slots.empty() && slots[0].kind == SlotKind::Section && slots[0].top == Layout::firstSection, "first section under the search field");
    Expect(slots.size() > 2 && slots[1].kind == SlotKind::Row && slots[1].top == Layout::firstSection + Downloads::DrawerLayout::sectionHeight + Downloads::DrawerLayout::toRows, "rows 12 DIP under their header");
    Expect(slots.size() > 3 && slots[3].kind == SlotKind::Section && slots[3].top == Layout::firstSection + Downloads::DrawerLayout::Block(2, 1), "next section after the block");
    open[0] = 0; auto folded = Slots(all, groups, open, 0);
    Expect(folded.size() == slots.size() - 2, "folded section hides its rows");
    std::vector<Entry> none; std::array<std::vector<int>, PeriodCount> empty;
    auto blank = Slots(none, empty, open, 0);
    Expect(blank.size() == 1 && blank[0].kind == SlotKind::Empty, "empty state slot");
    // Hit identifiers round-trip.
    Expect(HistoryUI::HitIndex(HistoryUI::ActionHit(41, 1)) == 41 && HistoryUI::HitPart(HistoryUI::ActionHit(41, 1)) == 2 && HistoryUI::HitPart(HistoryUI::RowHit(41)) == 0, "row hit ids");
    Expect(!HistoryUI::IsRowPart(HistoryUI::SectionHit(Period::LastYear)) && HistoryUI::IsSection(HistoryUI::SectionHit(Period::LastYear)), "section hit ids");
    // Removing a page (row action) is the address history's own removal.
    auto copy = history; Expect(copy.Remove(all[0].url) && Entries(copy).size() == 5, "remove from history");
    std::stringstream io; history.Write(io); auto back = AddressBar::History::Read(io);
    Expect(Entries(back).size() == 6 && Entries(back)[0].last == all[0].last, "persisted history lists the same pages");
}

struct Fixture {
    std::wstring root, out;
    bool Render(const wchar_t* name, UINT dpi, float height, const std::wstring& query, std::array<float, PeriodCount> open, float scroll,
                std::map<int, int> hovered = {}, int focus = 0, bool empty = false) {
        Chrome chrome;
        if (FAILED(chrome.InitOffscreen(root, dpi, Layout::width, height))) { std::fprintf(stderr, "offscreen init failed\n"); return false; }
        HistoryUI::View view(chrome);
        const auto history = Sample(noon);
        auto entries = empty ? std::vector<Entry>{} : Entries(history, query);
        const auto groups = Group(entries, noon, hour);
        float bottom = 0; const auto slots = Slots(entries, groups, open, scroll, &bottom);
        std::map<int, Downloads::RowReveal> reveals; const auto now = Motion::Time{} + std::chrono::seconds(100);
        for (auto [index, ms] : hovered) { auto& r = reveals[index]; if (ms < 0) r.Snap(true); else r.Set(true, now - Motion::Milliseconds(ms), false); }
        HistoryUI::Scene s; s.entries = &entries; s.slots = &slots; s.reveals = &reveals; s.query = query; s.focus = focus; s.keyboard = focus != 0 && focus != HistoryUI::SearchHit;
        s.caret = focus == HistoryUI::SearchHit; s.height = height; s.now = now; s.nowMs = noon; s.offsetMs = hour;
        s.fadeTop = Motion::Clamp(scroll / Downloads::DrawerLayout::topFade); s.fadeBottom = Motion::Clamp((bottom - height - scroll) / Downloads::Panel::pitch);
        auto* target = chrome.RenderTarget(); target->BeginDraw(); view.Paint(s);
        const HRESULT hr = target->EndDraw(); view.Release();
        if (FAILED(hr)) { std::fprintf(stderr, "EndDraw %08lx\n", static_cast<unsigned long>(hr)); return false; }
        return SUCCEEDED(chrome.SavePng((out + L"\\" + name).c_str()));
    }
};

int wmain(int argc, wchar_t** argv) {
    // This drawer's Paper copy is English: its strings are checked in an English process, set explicitly
    // (never the OS language). The French wording is checked in tests/i18n_test.cpp.
    I18n::SetForProcess(I18n::Locale::En);
    ModelChecks();
    if (argc >= 3) {
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
        Fixture f{argv[1], argv[2]};
        const std::array<float, PeriodCount> open{1, 1, 1, 1, 1};
        Expect(f.Render(L"history-180.png", 180, 728, L"", open, 0, {}, HistoryUI::SearchHit), "render drawer");
        Expect(f.Render(L"history-96.png", 96, 600, L"", open, 0, {}, HistoryUI::SearchHit), "render 100%");
        Expect(f.Render(L"history-hover.png", 180, 728, L"", open, 0, {{0, -1}}), "render row reveal");
        for (int ms : {0, 60, 120, 240}) {
            wchar_t name[64]; std::swprintf(name, 64, L"history-reveal-%03d.png", ms);
            Expect(f.Render(name, 180, 728, L"", open, 0, {{1, ms}}), "render reveal frame");
        }
        Expect(f.Render(L"history-focus-row.png", 180, 728, L"", open, 0, {{1, -1}}, HistoryUI::RowHit(1)), "render keyboard row focus");
        Expect(f.Render(L"history-focus-action.png", 180, 728, L"", open, 0, {{1, -1}}, HistoryUI::ActionHit(1, 1)), "render keyboard action focus");
        Expect(f.Render(L"history-search.png", 180, 728, L"git", open, 0, {}, HistoryUI::SearchHit), "render search");
        Expect(f.Render(L"history-no-results.png", 180, 728, L"rust", open, 0, {}, HistoryUI::SearchHit), "render no results");
        Expect(f.Render(L"history-empty.png", 180, 728, L"", open, 0, {}, HistoryUI::SearchHit, true), "render empty");
        Expect(f.Render(L"history-folded.png", 180, 728, L"", {1, 0, 1, 1, 1}, 0, {}, HistoryUI::SectionHit(Period::Yesterday)), "render folded section");
        Expect(f.Render(L"history-short.png", 180, 300, L"", open, 40), "render scrolled with edge fades");
        CoUninitialize();
    }
    if (failures) { std::fprintf(stderr, "%d history check(s) failed\n", failures); return 1; }
    std::printf("history panel checks passed\n");
    return 0;
}
