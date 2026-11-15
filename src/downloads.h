#pragma once
#include "i18n.h"
// Download history: engine-independent model, text formatting, persistence,
// panel geometry and the row hover reveal. No HWND, Direct2D or WebView2 here.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <istream>
#include <map>
#include <ostream>
#include <string>
#include <vector>
#include "motion.h"
#include "design-system.h"

namespace NativeChrome { namespace Downloads {
enum class State { InProgress, Paused, Completed, Failed, Cancelled };
struct Item {
    int id = 0;
    std::wstring path, uri, reason;  // reason: short failure text (Failed only)
    State state = State::InProgress;
    std::int64_t received = 0, total = 0; // total <= 0: unknown size, never simulated
    std::int64_t endTime = 0;             // estimated end, Unix ms; 0 = unknown
    std::int64_t started = 0;             // when the download began, Unix ms; 0 = unknown (oldest group)
    bool canResume = false, missing = false;
};
// Row actions, left to right. None marks an empty slot.
enum class Action { None, Pause, Resume, Cancel, Retry, Remove, ShowInFolder, Open };
struct Actions { Action first = Action::None, second = Action::None; };
inline Actions ActionsFor(const Item& item) {
    switch (item.state) {
    case State::InProgress: return {Action::Pause, Action::Cancel};
    case State::Paused: return {Action::Resume, Action::Cancel};
    case State::Completed: return item.missing ? Actions{Action::None, Action::Remove} : Actions{Action::ShowInFolder, Action::Open};
    case State::Failed: return item.canResume ? Actions{Action::Retry, Action::Remove} : Actions{Action::None, Action::Remove};
    case State::Cancelled: return {Action::None, Action::Remove};
    }
    return {};
}
inline const wchar_t* IconFor(Action action) {
    switch (action) {
    case Action::Pause: return L"pause";
    case Action::Resume: return L"resume";
    case Action::Cancel: case Action::Remove: return L"close";
    case Action::Retry: return L"retry";
    case Action::ShowInFolder: return L"folder";
    case Action::Open: return L"open";
    default: return nullptr;
    }
}
inline const wchar_t* TipFor(Action action) {
    switch (action) {
    case Action::Pause: return TR(downloads_action_pause);
    case Action::Resume: return TR(downloads_action_resume);
    case Action::Cancel: return TR(downloads_action_cancel);
    case Action::Retry: return TR(downloads_action_retry);
    case Action::Remove: return TR(downloads_action_remove);
    case Action::ShowInFolder: return TR(downloads_action_show_in_folder);
    case Action::Open: return TR(downloads_action_open);
    default: return L"";
    }
}

// ---- Text ---------------------------------------------------------------
inline std::wstring FileName(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}
// "In …/Downloads": the containing folder only, the rest elided.
inline std::wstring FolderLabel(const std::wstring& path) {
    auto end = path.find_last_of(L"\\/");
    if (end == std::wstring::npos || end == 0) return TR(downloads_done);
    auto start = path.find_last_of(L"\\/", end - 1);
    std::wstring folder = path.substr(start == std::wstring::npos ? 0 : start + 1, end - (start == std::wstring::npos ? 0 : start + 1));
    if (folder.empty()) return TR(downloads_done);
    if (folder.back() == L':') return TRF(downloads_in_drive, folder);  // drive root, e.g. "In D:"
    return TRF(downloads_in_folder, folder);
}
// Up to three significant figures, trailing ".0" dropped: 50.4, 200, 1.2.
inline std::wstring Number(double value) {
    wchar_t text[32];
    const double tenths = std::round(value * 10) / 10;
    if (tenths >= 100) std::swprintf(text, 32, L"%.0f", std::round(value));
    else std::swprintf(text, 32, L"%.1f", tenths);
    std::wstring s = text;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, L".0") == 0) s.resize(s.size() - 2);
    if (I18n::Current() == I18n::Locale::Fr) for (auto& ch : s) if (ch == L'.') ch = L',';   // « 50,4 »
    return s;
}
// Binary units, as Windows Explorer shows them. Both sides share the total's
// unit ("50.4/200MB"); an unknown total shows the received amount alone.
inline std::wstring Bytes(std::int64_t received, std::int64_t total) {
    const wchar_t* units[] = {TR(size_unit_b), TR(size_unit_kb), TR(size_unit_mb), TR(size_unit_gb), TR(size_unit_tb)};
    const double reference = static_cast<double>(std::max<std::int64_t>(total > 0 ? total : received, 0));
    int unit = 0; double scale = 1;
    while (unit < 4 && reference >= scale * 1024) { scale *= 1024; ++unit; }
    const double r = std::max<std::int64_t>(received, 0) / scale;
    if (total <= 0) return TRF(size_with_unit, Number(r), units[unit]);
    return TRF(size_with_unit, Number(std::min(r, total / scale)) + L"/" + Number(total / scale), units[unit]);
}
// "00:14", "12:03", "1:02:03". Negative/unknown is empty.
inline std::wstring Remaining(std::int64_t seconds) {
    if (seconds < 0) return L"";
    wchar_t text[32];
    const long long h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    if (h > 0) std::swprintf(text, 32, L"%lld:%02lld:%02lld", h, m, s);
    else std::swprintf(text, 32, L"%02lld:%02lld", m, s);
    return text;
}
// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant).
inline std::int64_t DaysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}
// WebView2 reports the estimate as ISO 8601 ("2024-05-01T12:00:00.123Z",
// optional offset). Returns Unix ms, or 0 when absent/unparseable.
inline std::int64_t ParseIsoTime(const std::wstring& text) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, consumed = 0;
    if (std::swscanf(text.c_str(), L"%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &s, &consumed) < 6 || consumed <= 0) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return 0;
    std::int64_t ms = ((DaysFromCivil(y, mo, d) * 24 + h) * 60 + mi) * 60000LL + s * 1000LL;
    size_t i = static_cast<size_t>(consumed);
    if (i < text.size() && text[i] == L'.') {
        int digits = 0, fraction = 0; ++i;
        while (i < text.size() && iswdigit(text[i])) { if (digits < 3) { fraction = fraction * 10 + (text[i] - L'0'); ++digits; } ++i; }
        while (digits < 3) { fraction *= 10; ++digits; }
        ms += fraction;
    }
    if (i < text.size() && (text[i] == L'+' || text[i] == L'-')) {
        int oh = 0, om = 0;
        if (std::swscanf(text.c_str() + i + 1, L"%2d:%2d", &oh, &om) >= 1) ms -= (text[i] == L'+' ? 1 : -1) * (oh * 60LL + om) * 60000LL;
    }
    return ms;
}
inline std::wstring FailureText(const std::wstring& reason) {
    if (reason.rfind(TR(downloads_blocked_prefix), 0) == 0) return reason; // engine security blocks carry their own wording (E6)
    return reason.empty() ? std::wstring(TR(downloads_failed)) : TRF(downloads_failed_reason, reason);
}


// ---- Periods (drawer sections) --------------------------------------------
// Local calendar days, newest first: today, yesterday, the rest of the last
// week, of the last month, then everything older ("Last year").
enum class Period { Today, Yesterday, LastWeek, LastMonth, LastYear };
constexpr int PeriodCount = 5;
inline const wchar_t* PeriodLabel(Period period) {
    switch (period) {
    case Period::Today: return TR(period_today);
    case Period::Yesterday: return TR(period_yesterday);
    case Period::LastWeek: return TR(period_last_week);
    case Period::LastMonth: return TR(period_last_month);
    default: return TR(period_last_year);
    }
}
inline std::int64_t FloorDiv(std::int64_t a, std::int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
// offset: local time minus UTC, in ms (the caller reads it from the OS).
inline Period PeriodOf(std::int64_t started, std::int64_t now, std::int64_t offset) {
    if (started <= 0) return Period::LastYear;
    const std::int64_t day = 86400000LL;
    const std::int64_t ago = FloorDiv(now + offset, day) - FloorDiv(started + offset, day);
    if (ago <= 0) return Period::Today;
    if (ago == 1) return Period::Yesterday;
    if (ago <= 7) return Period::LastWeek;
    if (ago <= 31) return Period::LastMonth;
    return Period::LastYear;
}

// ---- History persistence -------------------------------------------------
// Finished downloads only (completed / failed / cancelled), newest first,
// one UTF-8 line each: state<TAB>bytes<TAB>path<TAB>uri. In-progress
// operations belong to the engine and are not restored.
constexpr size_t HistoryLimit = 50;
inline std::string ToUtf8(const std::wstring& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        std::uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); ++i; }
        else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;   // review F2: a lone surrogate (any page title) is never written as bytes the reader rejects
        if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}
// Strict decoder: any malformed sequence rejects the line.
inline bool FromUtf8(const std::string& s, std::wstring& out) {
    out.clear();
    for (size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        std::uint32_t c; int extra;
        if (b < 0x80) { c = b; extra = 0; }
        else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; extra = 1; }
        else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; extra = 2; }
        else if ((b & 0xF8) == 0xF0) { c = b & 0x07; extra = 3; }
        else return false;
        if (i + static_cast<size_t>(extra) >= s.size()) return false;
        for (int k = 1; k <= extra; ++k) { const unsigned char n = static_cast<unsigned char>(s[i + k]); if ((n & 0xC0) != 0x80) return false; c = (c << 6) | (n & 0x3F); }
        if ((extra == 1 && c < 0x80) || (extra == 2 && c < 0x800) || (extra == 3 && (c < 0x10000 || c > 0x10FFFF)) || (c >= 0xD800 && c <= 0xDFFF)) return false;
        if (c >= 0x10000) { c -= 0x10000; out += static_cast<wchar_t>(0xD800 + (c >> 10)); out += static_cast<wchar_t>(0xDC00 + (c & 0x3FF)); }
        else out += static_cast<wchar_t>(c);
        i += 1 + extra;
    }
    return true;
}
inline bool Persistent(const Item& item) { return item.state == State::Completed || item.state == State::Failed || item.state == State::Cancelled; }
inline void WriteHistory(std::ostream& out, const std::vector<Item>& items) {
    out << "browser-downloads 2\n";
    size_t written = 0;
    for (const auto& item : items) {
        if (!Persistent(item) || item.path.empty() || written >= HistoryLimit) continue;
        const char state = item.state == State::Completed ? 'C' : item.state == State::Failed ? 'F' : 'X';
        out << state << '\t' << (item.total > 0 ? item.total : item.received) << '\t' << item.started << '\t' << ToUtf8(item.path) << '\t' << ToUtf8(item.uri) << '\t' << ToUtf8(item.reason) << '\n';
        ++written;
    }
}
// Interprocess three-way merge of the finished-downloads list (decision 2). A row is the same row
// with the same file, start time and source. Rows this window removed are removed from the file,
// rows another window removed are never brought back, rows finished or changed here replace or
// join the file's. In-progress rows (not persisted) of this window stay as they are; rows coming
// from the file get fresh ids. Result: this window's live rows, then the finished ones, newest first.
inline std::string HistoryKey(const Item& i) { return ToUtf8(i.path) + "\t" + std::to_string(i.started) + "\t" + ToUtf8(i.uri); }
inline bool SameRow(const Item& a, const Item& b) {
    return a.state == b.state && a.reason == b.reason && (a.total > 0 ? a.total : a.received) == (b.total > 0 ? b.total : b.received);
}
inline std::vector<Item> MergeHistory(const std::vector<Item>& base, const std::vector<Item>& mine, const std::vector<Item>& theirs, int& nextId) {
    std::map<std::string, const Item*> b, m;
    for (const auto& i : base) if (Persistent(i)) b[HistoryKey(i)] = &i;
    for (const auto& i : mine) if (Persistent(i)) m[HistoryKey(i)] = &i;
    std::vector<Item> live, done;
    for (const auto& i : mine) if (!Persistent(i)) live.push_back(i);
    std::map<std::string, bool> taken;
    for (const auto& t : theirs) {
        const std::string k = HistoryKey(t);
        if (b.count(k) && !m.count(k)) continue;                       // removed here
        const auto mi = m.find(k);
        if (mi != m.end() && (!b.count(k) || !SameRow(*b[k], *mi->second))) { done.push_back(*mi->second); taken[k] = true; continue; }   // changed here
        if (mi != m.end()) { Item keep = t; keep.id = mi->second->id; keep.missing = mi->second->missing; done.push_back(keep); taken[k] = true; continue; }
        Item other = t; other.id = nextId++; done.push_back(other);     // another window's row
    }
    for (const auto& [k, i] : m) if (!taken.count(k) && !b.count(k)) done.push_back(*i);   // new here
    std::stable_sort(done.begin(), done.end(), [](const Item& x, const Item& y) { return x.started > y.started; });
    if (done.size() > HistoryLimit) done.resize(HistoryLimit);
    live.insert(live.end(), done.begin(), done.end());
    return live;
}
// Unknown versions or malformed lines are skipped, never fatal. Version 1
// (no start time) still loads; its rows land in the oldest group.
inline std::vector<Item> ReadHistory(std::istream& in, int& nextId) {
    std::vector<Item> items; std::string line;
    if (!std::getline(in, line)) return items;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const int version = line == "browser-downloads 2" ? 2 : line == "browser-downloads 1" ? 1 : 0;
    if (!version) return items;
    while (items.size() < HistoryLimit && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> fields; size_t start = 0;
        for (;;) { auto tab = line.find('\t', start); fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start)); if (tab == std::string::npos) break; start = tab + 1; }
        std::int64_t started = 0;
        if (version == 2) {
            if (fields.size() < 5) continue;
            char* stop = nullptr; started = std::strtoll(fields[2].c_str(), &stop, 10);
            if (fields[2].empty() || !stop || *stop || started < 0) continue;
            fields.erase(fields.begin() + 2);
        }
        if (fields.size() < 4 || fields[0].size() != 1) continue;
        Item item;
        switch (fields[0][0]) { case 'C': item.state = State::Completed; break; case 'F': item.state = State::Failed; break; case 'X': item.state = State::Cancelled; break; default: continue; }
        char* end = nullptr; const long long bytes = std::strtoll(fields[1].c_str(), &end, 10);
        if (fields[1].empty() || !end || *end || bytes < 0) continue;
        if (!FromUtf8(fields[2], item.path) || item.path.empty() || !FromUtf8(fields[3], item.uri)) continue;
        if (fields.size() > 4 && !FromUtf8(fields[4], item.reason)) continue;
        item.received = item.total = bytes; item.started = started; item.id = nextId++;
        items.push_back(std::move(item));
    }
    return items;
}

// ---- Panel geometry (DIP) ------------------------------------------------
// Measured from the Paper "Download history" frame: 16 DIP padding, a 24 DIP
// header, rows of a 32 DIP tile + two 16 DIP lines on a 48 DIP pitch.
namespace Panel {
using Design::Box;
inline constexpr float width = 268, padding = Design::Space::lg, headerHeight = Design::Size::compact;
inline constexpr float tile = Design::Size::control, rowGap = Design::Space::lg, pitch = tile + rowGap;
inline constexpr float textLeft = padding + tile + Design::Space::md;           // 56
inline constexpr float line = 16, detailText = 14, detailIcon = 14, iconGap = Design::Space::xs, groupGap = Design::Space::sm;
inline constexpr float button = Design::Size::compact, buttonGap = Design::Space::sm;
inline constexpr float actionsRight = width - padding - Design::Space::sm;     // 246
inline constexpr float maskFade = 88;  // gradient over the name before the actions
inline constexpr float radius = 8;     // Windows 11 popup corner (DWM)
inline constexpr int popoverRows = 4;  // latest downloads; "View all" opens the drawer
inline constexpr float anchorGap = 8;  // below the toolbar button: top on the page edge (81), like the drawer
inline constexpr float headerTop = padding - (headerHeight - line) / 2;        // 12: header row centred on the first text line
inline constexpr float firstRow = headerTop + headerHeight + Design::Space::lg; // 52
inline constexpr float RowsBottom(int rows) { return firstRow + (rows < 1 ? 1 : rows) * pitch - rowGap; }
// "View all": one text line, 16 DIP under the last row.
inline constexpr float ViewAllTop(int rows) { return RowsBottom(rows) + rowGap; }
inline constexpr float Height(int rows, bool viewAll = false) {
    const int shown = rows < 1 ? 1 : (rows > popoverRows ? popoverRows : rows);
    return (viewAll ? ViewAllTop(shown) + line : RowsBottom(shown)) + padding;
}
inline constexpr Box Title() { return {padding, padding, width - padding - button, padding + line}; }
inline constexpr Box Close() { return {width - padding + (button - Design::Size::icon) / 2 - button, headerTop, width - padding + (button - Design::Size::icon) / 2, headerTop + button}; }
inline constexpr float RowTop(int index, float scroll = 0) { return firstRow + index * pitch - scroll; }
inline constexpr Box Tile(float top) { return {padding, top, padding + tile, top + tile}; }
// The name's line box sits 1.5 DIP high so its x-height centres like the Paper text.
inline constexpr float nameRaise = 1.5f;
inline constexpr Box Name(float top) { return {textLeft, top - nameRaise, width - padding, top + line - nameRaise}; }
inline constexpr Box Detail(float top) { return {textLeft, top + line, width - padding, top + 2 * line}; }
// Hover band: half the gap above and below so the pointer never falls between rows.
inline constexpr Box Hover(float top) { return {0, top - rowGap / 2, width, top + tile + rowGap / 2}; }
inline constexpr Box ActionBox(float top, int slot) { // slot 0 = left, 1 = right
    const float right = actionsRight - (1 - slot) * (button + buttonGap);
    return {right - button, top + (tile - button) / 2, right, top + (tile + button) / 2};
}
inline constexpr float MaskSolid() { return ActionBox(0, 0).left - Design::Space::sm; } // 186
inline constexpr float MaskStart() { return MaskSolid() - maskFade; }                    // 98
inline constexpr float ViewTop() { return firstRow - rowGap / 2; }
// Text-link hit area: the label plus 4 DIP around it.
inline constexpr Box ViewAll(float top, float labelWidth) { return {padding - 4, top - 4, padding + labelWidth + 4, top + line + 4}; }
}
// ---- Drawer geometry (DIP) -------------------------------------------------
// Measured from the Paper "Download history" drawer: the popover's width and
// header, docked to the window's right edge under the toolbar. Sections: a 24
// DIP header (label + caret aligned with the close button), 12 DIP, the rows,
// 12 DIP; four rows give the 240 DIP stride of the frame.
namespace DrawerLayout {
using Design::Box;
inline constexpr float width = Panel::width, top = Design::Vertical::top; // window client DIP: right under the chrome (81, no top padding)
// The popover floats 6 DIP in from the window edge; the drawer window keeps that
// strip (clipped by its region) so the popover rectangle can morph into it.
inline constexpr float margin = Design::Space::sm;
inline constexpr float firstSection = 59, sectionHeight = Design::Size::compact, toRows = 12, after = 12;
inline constexpr float viewTop = Panel::headerTop + Panel::headerHeight + Design::Space::md; // 44: content scrolls under the header
inline constexpr float topFade = 16, bottomFade = 128;
inline constexpr float Block(int rows, float open) { return sectionHeight + open * (toRows + rows * Panel::pitch) + after; }
inline constexpr Box Label(float y) { return {Panel::padding, y + (sectionHeight - Panel::line) / 2, Panel::width - Panel::padding - Panel::button, y + (sectionHeight + Panel::line) / 2}; }
inline constexpr Box Caret(float y) { return {Panel::Close().left, y, Panel::Close().right, y + sectionHeight}; }
inline constexpr Box Header(float y) { return {0, y, width, y + sectionHeight}; }
}
static_assert(DrawerLayout::Block(4, 1) == 240, "four rows: the 240 DIP section stride of the Paper drawer");
static_assert(Panel::Height(4, true) == 276, "four rows + View all match the Paper popover height");
static_assert(Panel::firstRow == 52, "rows start 16 DIP under the 24 DIP header");
static_assert(Panel::textLeft == 56, "row text 8 DIP after the 32 DIP tile");
static_assert(Panel::Height(4) == 244, "four rows match the first Paper frame height");
static_assert(Panel::ActionBox(0, 1).right == 246 && Panel::ActionBox(0, 0).right == 216, "actions: 24 DIP buttons, 6 DIP gap");

// ---- Row reveal motion ----------------------------------------------------
// Hovering a row slides the name mask and its two actions in from the right
// while they un-blur and fade in, staggered so the mask leads. Leaving reverses
// faster and together. Each lane retargets from its current value, so quick
// pointer sweeps never jump. Reduced motion keeps only a short cross-fade.
struct LayerVisual { float opacity = 0, offset = 0, blur = 0; };
// CSS-style cubic-bezier(x1, y1, x2, y2): bounded bisection on x, no overshoot.
struct Bezier {
    float x1, y1, x2, y2;
    float At(float x) const noexcept {
        x = Motion::Clamp(x);
        if (x == 0 || x == 1) return x;
        float lo = 0, hi = 1;
        for (int n = 0; n < 24; ++n) {
            const float t = (lo + hi) * .5f, u = 1 - t;
            if (3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t < x) lo = t; else hi = t;
        }
        const float t = (lo + hi) * .5f, u = 1 - t;
        return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t;
    }
};
// Critically damped spring from rest (Apple's damping 1.0): zero initial
// velocity, so the first frame never jumps, then a smooth settle with no
// overshoot. `response` is the spring's period (s); the duration it is driven
// over is Settle(response), where it is within 0.1% of its target.
struct Spring {
    float response;
    static constexpr float settle = 9.2334f; // omega * t where (1 + x) e^-x = 0.001
    float At(float x) const noexcept {
        x = Motion::Clamp(x);
        if (x >= 1) return 1;
        const float w = settle * x;
        return (1 - (1 + w) * std::exp(-w)) / .999f;
    }
};
constexpr Motion::Milliseconds Settle(float response) { return Motion::Milliseconds(static_cast<long long>(response * Spring::settle / 6.2831853f * 1000 + .5f)); }
// Lanes run either a cubic-bezier or a spring.
struct Easing {
    bool spring = false; Bezier bezier{.2f, 0, 0, 1}; Spring physics{.3f};
    constexpr Easing(Bezier b) : bezier(b) {}
    constexpr Easing(Spring p) : spring(true), physics(p) {}
    float At(float x) const noexcept { return spring ? physics.At(x) : bezier.At(x); }
};
namespace Curves {
inline constexpr Bezier emphasized{.2f, 0, 0, 1};   // Motion::Curve::Icon
inline constexpr Bezier drawer{.32f, .72f, 0, 1};   // iOS sheet curve: fast start, long settle
}
class Lane {
    float from_ = 0, to_ = 0;
    Motion::Time begin_{}, end_{};
    Easing curve_ = Curves::emphasized;
public:
    float Value(Motion::Time now) const noexcept {
        if (now >= end_) return to_;
        if (now <= begin_) return from_;
        return from_ + (to_ - from_) * curve_.At(Motion::Fraction(now, begin_, end_));
    }
    // Retargets from the value on screen now, so interruptions never jump.
    void Set(float target, Motion::Time now, Motion::Milliseconds delay, Motion::Milliseconds duration, Easing curve = Curves::emphasized) noexcept {
        if (target == to_) return;
        from_ = Value(now); to_ = target; curve_ = curve;
        if (duration.count() <= 0) { from_ = to_; begin_ = end_ = now; return; }
        begin_ = now + delay; end_ = begin_ + duration;
    }
    void Snap(float value) noexcept { from_ = to_ = value; begin_ = end_ = Motion::Time{}; }
    bool Moving(Motion::Time now) const noexcept { return from_ != to_ && now < end_; }
    float Target() const noexcept { return to_; }
};
namespace Reveal {
inline constexpr Motion::Milliseconds enter{240}, exit{140}, stagger{35}, fade{100};
inline constexpr float maskTravel = 24, buttonTravel = 12, maxBlur = 4;
}
class RowReveal {
    Lane lanes_[3]; // 0 = mask, 1 = left action, 2 = right action
    bool reduced_ = false;
public:
    void Set(bool shown, Motion::Time now, bool reduced) noexcept {
        reduced_ = reduced;
        for (int i = 0; i < 3; ++i) {
            if (reduced) lanes_[i].Set(shown ? 1.f : 0.f, now, Motion::Milliseconds(0), Reveal::fade);
            else if (shown) lanes_[i].Set(1, now, Reveal::stagger * i, Reveal::enter);
            else lanes_[i].Set(0, now, Motion::Milliseconds(0), Reveal::exit);
        }
    }
    void Snap(bool shown) noexcept { for (auto& lane : lanes_) lane.Snap(shown ? 1.f : 0.f); }
    LayerVisual Layer(int index, Motion::Time now) const noexcept {
        const float p = lanes_[index].Value(now);
        if (reduced_) return {p, 0, 0};
        const float travel = index == 0 ? Reveal::maskTravel : Reveal::buttonTravel;
        return {p, (1 - p) * travel, (1 - p) * Reveal::maxBlur};
    }
    bool Shown() const noexcept { return lanes_[0].Target() > 0; }
    bool Idle(Motion::Time now) const noexcept { for (auto& lane : lanes_) if (lane.Moving(now) || lane.Value(now) > 0) return false; return true; }
    Motion::Wake NextWake(Motion::Time now) const noexcept { for (auto& lane : lanes_) if (lane.Moving(now)) return Motion::Frame(); return {}; }
};
// Drawer: slides in from the window's right edge and leaves the same way
// (spatial consistency); sections fold on the same curve; wheel steps glide.
namespace DrawerMotion {
// Every drawer motion starts from rest on a critically damped spring (no click
// carries velocity, so nothing should leave at speed): enter, exit (snappier),
// section fold. Wheel steps keep the short emphasized curve.
inline constexpr Spring enterSpring{.32f}, exitSpring{.24f}, foldSpring{.26f};
inline constexpr Motion::Milliseconds enter = Settle(.32f), exit = Settle(.24f), fold = Settle(.26f), scroll{160};
// The morph rides a critically damped spring (response 0.32 s): it leaves the
// popover from rest, so the hand-off frame is identical to the popover.
inline constexpr Spring morphSpring{.32f};
inline constexpr Motion::Milliseconds morph = Settle(.32f);
// Morph: the drawer list glides from the popover's rows into place; content the
// popover did not show fades in over the last 75%; "View all" leaves in the first 30%.
inline constexpr float morphLead = .25f, morphLinkOut = .3f;
inline constexpr float foldTravel = 8; // rows tuck up under their header as they fold
}
// Panel entrance: content settles down 6 DIP while fading in.
namespace Entrance { inline constexpr Motion::Milliseconds duration{180}; inline constexpr float travel = 6; }
}} // namespace NativeChrome::Downloads
