#pragma once
// Browsing history panel model (toolbar History button, Ctrl+H): the pages kept by
// AddressBar::History, newest visit first, filtered by the panel's search query
// and grouped by local calendar period exactly like the download drawer. Pure
// C++17 (no HWND, Direct2D or WebView2) so tests/history_test.cpp exercises it.
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "address.h"

namespace NativeChrome { namespace HistoryView {
using Downloads::Period;
using Downloads::PeriodCount;

struct Entry {
    std::wstring url;
    std::wstring title;  // page title, or the readable address when the page had none
    std::wstring text;   // readable address (MatchText): searched, never displayed twice
    std::wstring host;   // detail line: "github.com", "Local file" for file:// pages
    std::wstring site;   // favicon key (AddressBar::SiteOf), empty without one
    std::int64_t last = 0; // last visit, Unix ms
    int visits = 0;
    bool local = false;  // file:// page
};

inline std::wstring HostOf(const std::wstring& url) {
    AddressBar::Parts p;
    if (!AddressBar::Split(url, p)) return url;
    if (p.scheme == L"file") return L"Local file";
    return AddressBar::ReadableHost(p.host);
}
inline Entry MakeEntry(const AddressBar::Visit& v) {
    Entry e;
    e.url = v.url; e.text = AddressBar::MatchText(v.url);
    const auto title = AddressBar::Trim(v.title);
    AddressBar::Parts p; e.local = AddressBar::Split(v.url, p) && p.scheme == L"file";
    // An untitled local page reads as its file name ("notes.html"), not its file:/// URL.
    const auto runs = e.local ? AddressBar::Describe(v.url).runs : std::vector<AddressBar::Run>{};
    e.title = !title.empty() ? title : !runs.empty() ? runs.back().text : e.text;
    e.host = HostOf(v.url); e.site = AddressBar::SiteOf(v.url);
    e.last = v.last; e.visits = v.visits;
    return e;
}
// Every whitespace-separated term of the query must appear (case-insensitively)
// in the title or the readable address: "git issues" finds github.com/…/issues.
inline bool Matches(const Entry& e, const std::wstring& query) {
    const std::wstring q = AddressBar::Lower(AddressBar::Trim(query));
    if (q.empty()) return true;
    const std::wstring title = AddressBar::Lower(e.title), text = AddressBar::Lower(e.text);
    size_t start = 0;
    while (start < q.size()) {
        const size_t end = q.find_first_of(L" \t", start);
        const std::wstring term = q.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!term.empty() && title.find(term) == std::wstring::npos && text.find(term) == std::wstring::npos) return false;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return true;
}
// Newest visit first; equal times keep the history's order.
inline std::vector<Entry> Entries(const AddressBar::History& history, const std::wstring& query = L"") {
    std::vector<Entry> out;
    for (const auto& v : history.Items()) {
        Entry e = MakeEntry(v);
        if (Matches(e, query)) out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.last > b.last; });
    return out;
}
// Indices into `entries` per period (Today … Last year), each newest first.
inline std::array<std::vector<int>, PeriodCount> Group(const std::vector<Entry>& entries, std::int64_t now, std::int64_t offset) {
    std::array<std::vector<int>, PeriodCount> groups;
    for (size_t i = 0; i < entries.size(); ++i) groups[static_cast<int>(Downloads::PeriodOf(entries[i].last, now, offset))].push_back(static_cast<int>(i));
    return groups;
}

// ---- Detail line --------------------------------------------------------------
// "github.com · 14:05" today and yesterday, "github.com · Nov 3" this year,
// "github.com · Nov 3, 2025" before. Times are local (offset = local - UTC, ms).
struct Civil { std::int64_t year; unsigned month, day; };
inline Civil CivilFromDays(std::int64_t z) { // Howard Hinnant, inverse of DaysFromCivil
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    return {static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2), m, d};
}
inline std::wstring ClockText(std::int64_t t, std::int64_t offset) {
    const std::int64_t day = 86400000LL, local = t + offset;
    const std::int64_t ms = local - Downloads::FloorDiv(local, day) * day;
    wchar_t text[16]; std::swprintf(text, 16, L"%02d:%02d", static_cast<int>(ms / 3600000), static_cast<int>(ms / 60000 % 60));
    return text;
}
inline std::wstring DateText(std::int64_t t, std::int64_t now, std::int64_t offset) {
    static const wchar_t* months[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    const std::int64_t day = 86400000LL;
    const Civil c = CivilFromDays(Downloads::FloorDiv(t + offset, day)), today = CivilFromDays(Downloads::FloorDiv(now + offset, day));
    wchar_t text[32];
    if (c.year == today.year) std::swprintf(text, 32, L"%ls %u", months[c.month - 1], c.day);
    else std::swprintf(text, 32, L"%ls %u, %lld", months[c.month - 1], c.day, static_cast<long long>(c.year));
    return text;
}
inline std::wstring WhenText(std::int64_t t, std::int64_t now, std::int64_t offset) {
    if (t <= 0) return L"";
    const Period p = Downloads::PeriodOf(t, now, offset);
    return p == Period::Today || p == Period::Yesterday ? ClockText(t, offset) : DateText(t, now, offset);
}
inline std::wstring Detail(const Entry& e, std::int64_t now, std::int64_t offset) {
    const auto when = WhenText(e.last, now, offset);
    return when.empty() ? e.host : e.host + L" · " + when;
}
inline std::wstring EmptyText(const std::wstring& query) {
    const auto q = AddressBar::Trim(query);
    return q.empty() ? std::wstring(L"No history yet") : L"No results for “" + q + L"”";
}

// ---- Geometry (DIP) -----------------------------------------------------------
// The download drawer's column (268 DIP, 16 DIP padding, 24 DIP header, 48 DIP
// rows, 24 DIP section headers), docked to the window's right edge under the
// toolbar, below the History button beside Downloads. One addition: a 32 DIP search
// field under the header, built like the address field (icon slot 4 DIP in,
// text at 32 DIP).
namespace Layout {
using Design::Box;
using Downloads::Panel::padding;
inline constexpr float width = Downloads::Panel::width, top = Downloads::DrawerLayout::top;
inline constexpr float searchTop = Downloads::Panel::headerTop + Downloads::Panel::headerHeight + Design::Space::md; // 44
inline constexpr float searchHeight = Design::Size::control;                                                     // 32
inline constexpr float viewTop = searchTop + searchHeight + Design::Space::md;                                  // 84
// The first section keeps the download drawer's 15 DIP under the top of the scrolling view.
inline constexpr float firstSection = viewTop + (Downloads::DrawerLayout::firstSection - Downloads::DrawerLayout::viewTop); // 99
inline constexpr Box Search() { return {padding, searchTop, width - padding, searchTop + searchHeight}; }
inline constexpr Box SearchIcon() { return {padding + Design::Address::slotInset, searchTop + (searchHeight - Design::Address::slot) / 2,
                                            padding + Design::Address::slotInset + Design::Address::slot, searchTop + (searchHeight + Design::Address::slot) / 2}; }
inline constexpr float textLeft = padding + Design::Address::textInset; // 48
inline constexpr Box Clear() { return {width - padding - Design::Address::slotInset - Design::Size::compact, SearchIcon().top, width - padding - Design::Address::slotInset, SearchIcon().bottom}; }
inline constexpr Box SearchText(bool clear) { return {textLeft, searchTop + (searchHeight - Downloads::Panel::line) / 2, clear ? Clear().left - Design::Space::xs : width - padding - Design::Space::md, searchTop + (searchHeight + Downloads::Panel::line) / 2}; }
// Focus ring of a row: the 32 DIP tile + text band, 6 DIP outside it (radius 6 + 6).
inline constexpr float rowRingInset = Design::Space::sm;
inline constexpr Box RowRing(float top) { return {padding - rowRingInset, top - rowRingInset, width - padding + rowRingInset, top + Downloads::Panel::tile + rowRingInset}; }
inline constexpr float rowRingRadius = Design::Radius::control + rowRingInset;
}
static_assert(Layout::viewTop == 84 && Layout::firstSection == 99, "search field between the header and the first section");
static_assert(Layout::Search().left == 16 && Layout::Search().right == 252, "search field on the 16 DIP padding");
static_assert(Layout::Clear().right == 248 && Layout::Clear().left == 224, "clear button 4 DIP inside the field, like the address icon slot");

// ---- Layout (slots) -------------------------------------------------------------
enum class SlotKind { Row, Section, Empty };
struct Slot {
    SlotKind kind = SlotKind::Row;
    int index = -1;  // Row: entry index; Section: period
    float top = 0, opacity = 1, open = 1;
    float clipTop = -1e6f, clipBottom = 1e6f;
};
// Sections in period order; rows of a folding section tuck under their header,
// clipped to the band it still uncovers (the download drawer's fold).
inline std::vector<Slot> Slots(const std::vector<Entry>& entries, const std::array<std::vector<int>, PeriodCount>& groups,
                               const std::array<float, PeriodCount>& open, float scroll, float* contentBottom = nullptr) {
    using Downloads::Panel::pitch; namespace DL = Downloads::DrawerLayout;
    std::vector<Slot> slots; float y = Layout::firstSection - scroll;
    if (entries.empty()) { slots.push_back({SlotKind::Empty, -1, y}); y += Downloads::Panel::tile; }
    for (int p = 0; p < PeriodCount; ++p) {
        const auto& rows = groups[p]; if (rows.empty()) continue;
        const float o = Motion::Clamp(open[p]);
        slots.push_back({SlotKind::Section, p, y, 1, o});
        if (o > .001f) {
            const float clipTop = y + DL::sectionHeight, clipBottom = clipTop + o * (DL::toRows + rows.size() * pitch);
            for (size_t k = 0; k < rows.size(); ++k) {
                Slot row{SlotKind::Row, rows[k], clipTop + DL::toRows + k * pitch - (1 - o) * Downloads::DrawerMotion::foldTravel, o, 1};
                if (o < 1) { row.clipTop = clipTop; row.clipBottom = clipBottom; }
                if (row.top < clipBottom) slots.push_back(row);
            }
        }
        y += DL::Block(static_cast<int>(rows.size()), o);
    }
    if (contentBottom) *contentBottom = y + scroll + Downloads::Panel::padding - DL::after;
    return slots;
}
}} // namespace NativeChrome::HistoryView
