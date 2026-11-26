#pragma once
#include "i18n.h"
// Download history: engine-independent model, text formatting, persistence,
// panel geometry and the row hover reveal. No HWND, Direct2D or WebView2 here.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    // InProgress only: every byte is in, but the engine holds the file for the user's decision (Microsoft Defender
    // SmartScreen: a rarely downloaded file it could not confirm as safe). WebView2 reports nothing else (state
    // IN_PROGRESS, no interrupt reason) and keeps it so until Keep or Discard; it deletes the file when it exits.
    bool held = false;
};
// Row actions, left to right. None marks an empty slot.
enum class Action { None, Pause, Resume, Cancel, Retry, Remove, ShowInFolder, Open, Discard, Keep };
struct Actions { Action first = Action::None, second = Action::None; };
inline Actions ActionsFor(const Item& item) {
    switch (item.state) {
    case State::InProgress: return item.held ? Actions{Action::Discard, Action::Keep} : Actions{Action::Pause, Action::Cancel};
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
    case Action::Discard: return L"trash";
    case Action::Keep: return L"check";
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
    case Action::Discard: return TR(downloads_action_discard);
    case Action::Keep: return TR(downloads_action_keep);
    default: return L"";
    }
}

// ---- Engine state → row state (WebView2 values, kept numeric: no SDK header here) ---------------------
// COREWEBVIEW2_DOWNLOAD_STATE: 0 in progress, 1 interrupted, 2 completed. Interrupt reasons used here:
// USER_CANCELED 26, USER_SHUTDOWN 27, USER_PAUSED 28 (static_assert in main.cpp).
namespace Engine {
inline constexpr int InProgress = 0, Interrupted = 1, Completed = 2;
inline constexpr int ReasonUserCanceled = 26, ReasonUserShutdown = 27, ReasonUserPaused = 28;
// A download whose bytes are all in and that the engine still reports in progress for this long is held for the
// user's decision (measured: a finished download completes within a few hundred ms of its last byte; a held one
// never does). A late completion after the grace still lands as Completed.
inline constexpr std::int64_t HoldGraceMs = 2000;
}
inline State MapEngineState(int state, int reason, bool userPaused) {
    if (state == Engine::Completed) return State::Completed;
    if (state == Engine::InProgress) return userPaused ? State::Paused : State::InProgress;
    if (reason == Engine::ReasonUserPaused) return State::Paused;
    if (reason == Engine::ReasonUserCanceled) return State::Cancelled;
    return State::Failed;
}
// fullSinceMs: how long every byte has been in (negative: not all in, or the size is unknown).
inline bool HeldForDecision(int state, std::int64_t received, std::int64_t total, bool userPaused, std::int64_t fullSinceMs) {
    return state == Engine::InProgress && !userPaused && total > 0 && received >= total && fullSinceMs >= Engine::HoldGraceMs;
}
// « Conserver » on a held download (src/main.cpp KeepHeldDownload, WaitEnginePass, WatchKept): the decisions. Measured
// on WebView2 158 (tests/downloads/download_engine_probe.py --observe): after a cancel the engine removes its file by
// name when its own pass on the file ends (the pass that writes the zone mark): at once when it was over, seconds later
// when it still ran, and a removal can come again. So the engine is let go only once that pass is over (the zone mark)
// or after PassCapMs, and the path is then watched: every removal is undone from our own name for GuardMs after the
// last one; our name goes only then, with the file at its path proven ours. A file the engine marked needs no second
// attachment services pass; when one runs, our name stays alive across it (measured: its Save() removed the file once).
namespace Keep {
inline constexpr int StepMs = 100;
inline constexpr std::int64_t PassCapMs = 20000;        // the engine's own pass on the held file, waited for at most
inline constexpr std::int64_t GuardMarkedMs = 60000;    // watch after a file given back, its pass over at the cancel
inline constexpr std::int64_t GuardForcedMs = 300000;   // the same when the cap forced the cancel
enum class At { Gone, Pending, Left };
// From GetFileAttributesW (attributesRead, error) and an attempt to open the path: gone only on « not found »
// (2 ERROR_FILE_NOT_FOUND, 3 ERROR_PATH_NOT_FOUND); a name whose removal is pending cannot be opened.
inline At PathState(bool attributesRead, unsigned long error, bool opens) {
    if (!attributesRead && (error == 2 || error == 3)) return At::Gone;
    return attributesRead && opens ? At::Left : At::Pending;
}
// Before the cancel. engineState / reason: the operation's (Engine values); marked: the held file carries its zone mark;
// waitedMs: since « Conserver ». LetGo: the engine let go without a verdict (a closed tab, its shutdown); Ended: it
// finished the download or blocked it by itself, its outcome stands.
enum class Before { Wait, Cancel, LetGo, Ended };
inline Before BeforeCancel(int engineState, int reason, bool marked, std::int64_t waitedMs, std::int64_t capMs = PassCapMs) {
    if (engineState == Engine::InProgress) return marked || waitedMs >= capMs ? Before::Cancel : Before::Wait;
    if (engineState == Engine::Interrupted && (reason == Engine::ReasonUserCanceled || reason == Engine::ReasonUserShutdown)) return Before::LetGo;
    return Before::Ended;
}
inline std::int64_t GuardMs(bool markedAtCancel) { return markedAtCancel ? GuardMarkedMs : GuardForcedMs; }
// After the cancel, at each look at the path. seen: a removal seen since the cancel; sinceGivenMs: since the file was
// last given back. No short limit before the first removal: only a removal seen ends the wait (or the window's exit).
enum class Next { Wait, GiveBack, Guard, Settle };
inline Next Look(bool seen, At at, std::int64_t sinceGivenMs, std::int64_t guardMs) {
    if (at == At::Gone) return Next::GiveBack;
    if (!seen) return Next::Wait;
    return sinceGivenMs >= guardMs ? Next::Settle : Next::Guard;
}
inline int LookAfterMs(int looks) { return looks < 600 ? StepMs : 1000; }   // once a second after the first minute
// An insecure-download hold: a plain http:// file from a host that is not this computer (held by Chromium's insecure-
// download warning when the page is https). Measured: the engine runs no pass on it (never a zone mark) and removes it
// at once and once after the cancel: nothing to wait for, its pass counts as over.
inline bool InsecureSource(const std::wstring& uri) {
    static const wchar_t scheme[] = L"http://";
    if (uri.size() <= 7) return false;
    for (size_t i = 0; i < 7; ++i) if (std::towlower(uri[i]) != scheme[i]) return false;
    std::wstring host = uri.substr(7, uri.find_first_of(L"/?#", 7) == std::wstring::npos ? std::wstring::npos : uri.find_first_of(L"/?#", 7) - 7);
    const auto at = host.rfind(L'@'); if (at != std::wstring::npos) host = host.substr(at + 1);
    if (!host.empty() && host[0] == L'[') host = host.substr(0, host.find(L']') + 1); else host = host.substr(0, host.find(L':'));
    for (auto& c : host) c = static_cast<wchar_t>(std::towlower(c));
    return !host.empty() && host != L"localhost" && host.rfind(L"127.", 0) != 0 && host != L"[::1]";
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
// Failure reasons the browser generates are stored as a stable code, « @<catalog id> » (the downloads file
// keeps its format), and rendered in the interface language here. Rows saved before the codes (generated
// text in French or English) are recognised too; any other text is shown as it was stored.
inline constexpr I18n::Id GeneratedReasons[] = {
    I18n::Id::download_error_access_denied, I18n::Id::download_error_disk_full, I18n::Id::download_error_name_too_long,
    I18n::Id::download_error_too_large, I18n::Id::download_error_offline, I18n::Id::download_error_network,
    I18n::Id::download_error_interrupted, I18n::Id::download_error_server, I18n::Id::download_error_file,
    I18n::Id::download_blocked_dangerous, I18n::Id::download_blocked_check_failed, I18n::Id::download_blocked_policy,
    I18n::Id::downloads_unavailable,
};
inline bool BlockedReason(I18n::Id id) {
    return id == I18n::Id::download_blocked_dangerous || id == I18n::Id::download_blocked_check_failed || id == I18n::Id::download_blocked_policy;
}
inline std::wstring ReasonCode(I18n::Id id) { const char* n = I18n::EntryOf(id).name; return L"@" + std::wstring(n, n + std::strlen(n)); }
inline const I18n::Id* GeneratedReason(const std::wstring& stored) {
    for (const auto& id : GeneratedReasons) {
        const auto& e = I18n::EntryOf(id);
        if (stored == ReasonCode(id) || stored == e.fr || stored == e.en) return &id;
    }
    return nullptr;
}
inline std::wstring FailureText(const std::wstring& reason) {
    if (const I18n::Id* id = GeneratedReason(reason))   // engine security blocks carry their own wording (E6)
        return BlockedReason(*id) ? std::wstring(I18n::T(*id)) : TRF(downloads_failed_reason, I18n::T(*id));
    if (!reason.empty() && reason[0] == L'@') return TR(downloads_failed);   // a code from a newer version: never shown raw
    const auto& blocked = I18n::EntryOf(I18n::Id::downloads_blocked_prefix);
    if (reason.rfind(blocked.fr, 0) == 0 || reason.rfind(blocked.en, 0) == 0) return reason;
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
// Measured from the Paper "Download history" frame: 16 DIP padding, rows of a
// 32 DIP tile + two 16 DIP lines on a 48 DIP pitch. The downloads surfaces have
// no header any more (no title, no close button: Échap, a click outside or the
// toolbar button close them): the rows start at the top padding. headerTop,
// headerHeight, Title() and Close() stay: the history and bookmarks drawers
// keep that header, and the section carets stay on Close()'s column.
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
inline constexpr float firstRow = padding;                                     // 16: no header, rows start at the top padding
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
// ---- Held download card (DIP), Paper B07b « A · Ambre — à confirmer » ------------------------------------
// The popover shows a download held for the user's decision as a card in its row's place: amber tint (#E6AC3D14,
// 1 px inset #E6AC3D33, radius 6), 12 inside; a 32 tile (#E6AC3D26, radius 6, warning glyph 16 in #E6AC3D) 10 before
// the name (16 medium, line 20) and the status (14, line 18, amber) 2 under it; 12 below, the body (14, line 18,
// #FFFFFFB3); 12 below, two equal 32 buttons 8 apart: « Supprimer » (filled #FFFFFF1A, white) and « Conserver »
// (ghost, #FFFFFF99), 14 medium. Neighbouring rows' tiles sit 20 from the card (12 gap + the row's 8 inset).
namespace HeldCard {
using Design::Box;
inline constexpr float inset = 12, tile = 32, glyph = 16, radius = 6, headGap = 10, nameLine = 20, statusLine = 18, textGap = 2;
inline constexpr float head = nameLine + textGap + statusLine;   // 40: the text column sets the head's height
inline constexpr float gap = 12, bodyLine = 18, button = 32, buttonGap = 8, around = 20;
inline constexpr float left = Panel::padding, right = Panel::width - Panel::padding;
inline constexpr float innerWidth = right - left - 2 * inset;     // 212 in the 268 DIP popover
inline constexpr float textLeft = left + inset + tile + headGap;
inline constexpr float Height(float bodyHeight) { return inset + head + gap + bodyHeight + gap + button + inset; }
inline constexpr Box Card(float top, float height) { return {left, top, right, top + height}; }
inline constexpr Box Tile(float top) { return {left + inset, top + inset + (head - tile) / 2, left + inset + tile, top + inset + (head + tile) / 2}; }
inline constexpr Box Name(float top) { return {textLeft, top + inset, right - inset, top + inset + nameLine}; }
inline constexpr Box Status(float top) { return {textLeft, top + inset + nameLine + textGap, right - inset, top + inset + head}; }
inline constexpr float BodyTop(float top) { return top + inset + head + gap; }
// slot 0 = « Supprimer » (left), 1 = « Conserver » (right).
inline constexpr Box Button(float top, float height, int slot) {
    const float w = (innerWidth - buttonGap) / 2, x = left + inset + slot * (w + buttonGap), y = top + height - inset - button;
    return {x, y, x + w, y + button};
}
}
static_assert(HeldCard::innerWidth == 212 && HeldCard::Height(0) == 120, "held card: 12 inside, 40 head, 12, body, 12, 32 buttons, 12");

// ---- Drawer geometry (DIP) -------------------------------------------------
// Measured from the Paper "Download history" drawer: the popover's width,
// docked to the window's right edge under the toolbar, without a header: the
// first section's label sits on the 16 DIP top padding. Sections: a 24 DIP
// header (label + caret on the history drawer's close-button column), 12 DIP,
// the rows, 12 DIP; four rows give the 240 DIP stride of the frame.
namespace DrawerLayout {
using Design::Box;
inline constexpr float width = Panel::width, top = Design::Vertical::top; // window client DIP: right under the chrome (81, no top padding)
// Where the docked drawers (downloads, history, settings, bookmarks) start now: `top`, or in Mode Focus 4 DIP under the
// floating panel (src/focus-mode.h DrawerTop). Set by the host before it places them; UI thread only.
inline float liveTop = top;
// Mode Focus also lines the drawers' right edge up with the floating panel's (4 DIP in from the window's, `liveRight`)
// and rounds their two top corners (`liveRadius`, 6 DIP, src/drawer-corners.h). 0 / 0 outside focus mode.
inline float liveRight = 0, liveRadius = 0;
// Mode Focus also keeps them 4 DIP above the window's bottom edge (Paper « Annotation + Focus · barre du haut · tiroir
// ouvert »: 612 high from 124 in a 740 window), their bottom corners square. 0 outside focus mode.
inline float liveBottom = 0;
// The popover floats 6 DIP in from the window edge; the drawer window keeps that
// strip (clipped by its region) so the popover rectangle can morph into it.
inline constexpr float margin = Design::Space::sm;
inline constexpr float sectionHeight = Design::Size::compact, toRows = 12, after = 12;
inline constexpr float firstSection = Panel::padding - (sectionHeight - Panel::line) / 2; // 12: the label's line on the top padding
inline constexpr float viewTop = 0;                                                  // no header: content scrolls to the top edge
inline constexpr float topFade = 16, bottomFade = 128;
inline constexpr float Block(int rows, float open) { return sectionHeight + open * (toRows + rows * Panel::pitch) + after; }
inline constexpr Box Label(float y) { return {Panel::padding, y + (sectionHeight - Panel::line) / 2, Panel::width - Panel::padding - Panel::button, y + (sectionHeight + Panel::line) / 2}; }
inline constexpr Box Caret(float y) { return {Panel::Close().left, y, Panel::Close().right, y + sectionHeight}; }
inline constexpr Box Header(float y) { return {0, y, width, y + sectionHeight}; }
}
static_assert(DrawerLayout::Block(4, 1) == 240, "four rows: the 240 DIP section stride of the Paper drawer");
static_assert(Panel::Height(4, true) == 240, "four rows + View all: the Paper popover height (276) without its 36 DIP header");
static_assert(Panel::firstRow == 16 && DrawerLayout::firstSection == 12 && DrawerLayout::viewTop == 0, "no header: content starts at the top padding");
static_assert(Panel::textLeft == 56, "row text 8 DIP after the 32 DIP tile");
static_assert(Panel::Height(4) == 208, "four rows: the first Paper frame height (244) without its 36 DIP header");
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
// #127 motion spec §3 (Designer): drawers open on 1.0 / .30 s and close on 1.0 / .25 s, the same path both ways.
inline constexpr Spring enterSpring{.30f}, exitSpring{.25f}, foldSpring{.26f};
inline constexpr Motion::Milliseconds enter = Settle(.30f), exit = Settle(.25f), fold = Settle(.26f), scroll{160};
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
