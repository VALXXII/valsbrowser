#pragma once
// « Nouveautés » release notes (Paper p-L-0: 142E-0 Mini, 144O-0 Full opened). Model and decisions
// only; the modal draws it (src/modal.h, Spec::notes) and the host wires it (src/elements-integration.h).
//
// Contract (design/release-notes-modal-report.md, revised by design/release-notes-fix-report.md and
// design/release-notes-cumul-report.md):
//   * The notes are compiled into the build: nothing is fetched. Their text lives in
//     release-notes/notes.json (FR + EN, the next release's notes, release-notes/README.md). The notes of
//     the earlier releases are read back from that file's history on main (tools/release/release_notes.py):
//     one entry per version that changed it, keyed by that version. tools/release/build.py generates
//     <out>-obj/gen/release-notes-data.h, included below. A build without it (test hosts) has no notes.
//   * Read state: <global data root>\release-notes « browser-release-notes 1 » / « seen=<version> », the
//     version up to which the user has seen the notes. Builds 0.3.842 to 0.3.898 wrote « seen=notes-<hash> »
//     (mapped back to the version of the entry with that hash) and builds up to 0.3.840 the fixed id
//     LegacyId (= the notes shown at 0.3.646). Written atomically only once the card has REALLY been shown
//     (or silently at a first install). An unreadable, future or damaged file is never overwritten and never
//     triggers the card.
//   * Automatic Mini, once: only in the process the update helper launched after a successful update
//     (UpdateCore engine view == Installed at start, once per install) AND <installRoot>\previous records a
//     different installed version or a portable copy, AND at least one version newer than the one last seen
//     (else: newer than the previous installed version) and not newer than this build has notes. The card
//     shows those versions' notes together (newest first, de-duplicated, at most DisplayLines per section) and,
//     when they come from several versions, « Depuis la version <last seen> » under its title (Paper 26KK-0).
//     No unseen notes: no card, the generic « Mise à jour installée » notification. A first install, a repair
//     of the same version, a dev/portable run, another window or a normal launch never get it.
//   * Manual: À propos › « Notes de version » opens the Full view of the most recent version's notes (never
//     writes the state); a build without notes opens the release's GitHub page instead.
#include <algorithm>
#include <string>
#include <vector>
#include "i18n.h"
#include "profile-store.h"

namespace ReleaseNotes {

enum class Kind { Added, Fixed };
struct Section { Kind kind = Kind::Added; std::wstring title; std::vector<std::wstring> items; };

inline bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    return true;
}
inline bool ValidVersion(const std::string& v) {   // M.m.B, digits only, each part 1..5 digits
    int parts = 0; std::size_t run = 0;
    for (char c : v) {
        if (c >= '0' && c <= '9') { if (++run > 5) return false; }
        else if (c == '.') { if (!run) return false; ++parts; run = 0; }
        else return false;
    }
    return run && parts == 2;
}
// <0, 0, >0 like strcmp, on M.m.B numbers (both must be ValidVersion).
inline int CompareVersions(const std::string& a, const std::string& b) {
    long pa[3] = {}, pb[3] = {}; int i = 0, j = 0;
    for (char c : a) { if (c == '.') ++i; else if (i < 3) pa[i] = pa[i] * 10 + (c - '0'); }
    for (char c : b) { if (c == '.') ++j; else if (j < 3) pb[j] = pb[j] * 10 + (c - '0'); }
    for (int k = 0; k < 3; ++k) if (pa[k] != pb[k]) return pa[k] < pb[k] ? -1 : 1;
    return 0;
}

// One line of notes, French and English; one version's notes as generated (null-terminated lists).
struct Line { const wchar_t* fr = nullptr; const wchar_t* en = nullptr; };
struct RawEntry { const char* version; const char* id; const Line* added; const Line* fixed; };

} // namespace ReleaseNotes
#if defined(BROWSER_HAS_BUILD_INFO) && defined(__has_include)
#if __has_include("release-notes-data.h")
#include "release-notes-data.h"   // ReleaseNotes::Data::kEntries, from release-notes/notes.json and its history
#endif
#endif
namespace ReleaseNotes {

// Lines per section a card holds (Paper 26KJ-0, owner's decision 2026-11-25: 10 per group, the Full view scrolls
// past 480 DIP): the newest, most important ones first.
inline constexpr std::size_t DisplayLines = 10;
// The fixed id builds up to 0.3.840 recorded, and the version whose notes it stood for (seen at 0.3.646).
inline constexpr const char* LegacyId = "2026-11-comment-tooltips-download-selectall-typing";
inline constexpr const char* LegacyVersion = "0.3.646";

struct Entry { std::string version, id; std::vector<Line> added, fixed; };   // id: « notes-<hash> » of its text
// What one card shows. `id`: the version it brings the user up to (written as « seen » once shown).
// `since`: the version whose notes were seen last ("" unknown); `versions`: how many versions the lines come from.
struct Content {
    std::string id, since;
    std::vector<Line> added, fixed;   // « Ajouts » / « Corrections »; an empty one is not drawn
    int versions = 0;
    bool Empty() const { return !ValidId(id) || (added.empty() && fixed.empty()); }
    std::size_t Lines() const { return added.size() + fixed.size(); }
};
namespace Detail {
inline void Append(std::vector<Line>& out, const Line* lines) { for (; lines && lines->fr && lines->en; ++lines) out.push_back(*lines); }
}
inline std::vector<Entry> FromRaw(const RawEntry* raw) {
    std::vector<Entry> out;
    for (; raw && raw->version; ++raw) {
        if (!ValidVersion(raw->version)) continue;
        Entry e; e.version = raw->version; e.id = raw->id ? raw->id : "";
        Detail::Append(e.added, raw->added); Detail::Append(e.fixed, raw->fixed);
        if (!e.added.empty() || !e.fixed.empty()) out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return CompareVersions(a.version, b.version) > 0; });
    return out;
}
// Every version of this build that brought notes, newest first (empty when the build has none).
inline const std::vector<Entry>& History() {
    static const std::vector<Entry> history = [] {
#ifdef BROWSER_RELEASE_NOTES_HISTORY
        return FromRaw(Data::kEntries);
#else
        return std::vector<Entry>{};
#endif
    }();
    return history;
}
// The notes of every version in (after, upTo] (after empty: from the start), newest first, a line already
// taken (same French text) not repeated, at most `perSection` lines per section (0: all). Content::id = upTo.
inline Content Unseen(const std::vector<Entry>& history, const std::string& after, const std::string& upTo, std::size_t perSection = DisplayLines) {
    Content c;
    if (!ValidVersion(upTo) || (!after.empty() && !ValidVersion(after))) return c;
    c.id = upTo; c.since = after;
    std::vector<std::wstring> taken;
    const auto take = [&](std::vector<Line>& out, const Line& l) {
        const std::wstring fr = l.fr;
        if (std::find(taken.begin(), taken.end(), fr) != taken.end() || (perSection && out.size() >= perSection)) return;
        taken.push_back(fr); out.push_back(l);
    };
    for (const auto& e : history) {
        if (!ValidVersion(e.version) || CompareVersions(e.version, upTo) > 0 || (!after.empty() && CompareVersions(e.version, after) <= 0)) continue;
        ++c.versions;
        for (const auto& l : e.added) take(c.added, l);
        for (const auto& l : e.fixed) take(c.fixed, l);
    }
    return c;
}
// The most recent version's notes alone (À propos › Notes de version).
inline Content Latest(const std::vector<Entry>& history) {
    if (history.empty()) return Content{};
    const std::string& newest = history.front().version;
    std::string before;
    for (const auto& e : history) if (CompareVersions(e.version, newest) < 0) { before = e.version; break; }
    Content c = Unseen(history, before, newest);
    c.since.clear();   // one version: no « Depuis la version » line
    return c;
}
// « Depuis la version 0.3.646 » under the title of a card that gathers several versions (Paper 26KS-0); one version,
// or no known starting version: none (the card as it was, 144O-0).
inline std::wstring Subtitle(const Content& content) {
    if (content.versions < 2 || content.since.empty()) return L"";
    return TRF(release_notes_since, std::wstring(content.since.begin(), content.since.end()));
}
// The card's sections in the process language (Paper p-L-0 142E-0 / 144O-0: « Ajouts » then « Corrections »).
inline std::vector<Section> Sections(const Content& content) {
    std::vector<Section> out;
    if (content.Empty()) return out;
    const bool en = I18n::Current() == I18n::Locale::En;
    const auto add = [&](Kind kind, const wchar_t* title, const std::vector<Line>& lines) {
        if (lines.empty()) return;
        Section s; s.kind = kind; s.title = title;
        for (const auto& l : lines) s.items.push_back(en ? l.en : l.fr);
        out.push_back(std::move(s));
    };
    add(Kind::Added, TR(release_notes_added), content.added);
    add(Kind::Fixed, TR(release_notes_fixed), content.fixed);
    return out;
}
// Everything the notes say, in reading order (UIA description: the hidden part of the Mini included).
inline std::wstring Spoken(const std::vector<Section>& sections) {
    std::wstring s;
    for (const auto& sec : sections) {
        if (!s.empty()) s += L" ";
        s += sec.title + L".";
        for (const auto& item : sec.items) s += L" " + item;
    }
    return s;
}

// ---- read state ---------------------------------------------------------------------------------
inline constexpr const wchar_t* FileName = L"release-notes";
inline constexpr const char* Header = "browser-release-notes 1";
inline constexpr const char* HeaderFamily = "browser-release-notes ";
enum class Status { Ok, Missing, Unreadable, UnknownVersion, Invalid };
struct State { std::string seen; Status status = Status::Missing; };
inline const char* StatusText(Status s) {
    switch (s) {
    case Status::Ok: return "ok"; case Status::Missing: return "missing"; case Status::Unreadable: return "unreadable";
    case Status::UnknownVersion: return "unknown-version"; default: return "invalid";
    }
}
namespace Detail {
inline std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> out; std::size_t at = 0;
    while (at < bytes.size()) {
        std::size_t end = bytes.find('\n', at); if (end == std::string::npos) end = bytes.size();
        std::string l = bytes.substr(at, end - at); if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l); at = end + 1;
    }
    if (!out.empty() && out[0].size() >= 3 && static_cast<unsigned char>(out[0][0]) == 0xEF && static_cast<unsigned char>(out[0][1]) == 0xBB && static_cast<unsigned char>(out[0][2]) == 0xBF) out[0].erase(0, 3);
    return out;
}
}
inline State Parse(const std::string& bytes) {
    State r;
    if (bytes.empty()) return r;   // Missing
    const auto lines = Detail::Lines(bytes);
    if (lines.empty() || lines[0] != Header) { r.status = !lines.empty() && lines[0].rfind(HeaderFamily, 0) == 0 ? Status::UnknownVersion : Status::Invalid; return r; }
    r.status = Status::Invalid;
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto& l = lines[i];
        if (l.empty()) continue;
        if (l.rfind("seen=", 0) == 0 && ValidId(l.substr(5)) && r.status != Status::Ok) { r.seen = l.substr(5); r.status = Status::Ok; }
        else { r.seen.clear(); r.status = Status::Invalid; return r; }   // a second « seen » or anything else: not ours to guess
    }
    return r;
}
inline std::string Format(const std::string& id) { return std::string(Header) + "\nseen=" + id + "\n"; }
inline std::wstring PathIn(const std::wstring& dataRoot) { return dataRoot + L"\\" + FileName; }
inline State Load(const std::wstring& dataRoot) {
    std::string bytes;
    if (dataRoot.empty() || !ProfileStore::ReadAll(PathIn(dataRoot), bytes)) { State r; r.status = Status::Unreadable; return r; }
    return Parse(bytes);
}
// Writes only over a missing or our-own-format file: an unreadable, damaged or future file is kept.
inline bool MarkSeen(const std::wstring& dataRoot, const std::string& id) {
    if (!ValidId(id)) return false;
    const State now = Load(dataRoot);
    if (now.status != Status::Ok && now.status != Status::Missing) return false;
    if (now.status == Status::Ok && now.seen == id) return true;
    return ProfileStore::WriteAtomic(PathIn(dataRoot), Format(id));
}

// ---- <installRoot>\previous (written by the update helper; read only here) ----------------------
struct Previous { enum class Kind { None, Installed, Portable, Invalid } kind = Kind::None; std::string version; };
inline Previous ParsePrevious(const std::string& bytes) {
    Previous p;
    if (bytes.empty()) return p;   // None: never updated through the helper (first install)
    const auto lines = Detail::Lines(bytes);
    p.kind = Previous::Kind::Invalid;
    if (lines.empty() || bytes.size() > 4096) return p;
    for (size_t i = 1; i < lines.size(); ++i) if (!lines[i].empty()) return p;   // one record only
    const auto& l = lines[0];
    if (l.rfind("installed ", 0) == 0 && ValidVersion(l.substr(10))) { p.kind = Previous::Kind::Installed; p.version = l.substr(10); }
    else if (l.rfind("portable ", 0) == 0 && l.size() > 9) { p.kind = Previous::Kind::Portable; }
    return p;
}

// ---- automatic display ----------------------------------------------------------------------------
// The version up to which the user has seen the notes, from the read state: a version as such; the fixed id of
// builds up to 0.3.840 = LegacyVersion; a « notes-<hash> » of builds 0.3.842-0.3.898 = the version of the entry
// with that text; anything else (or no state): unknown, "".
inline std::string SeenVersion(const State& state, const std::vector<Entry>& history) {
    if (state.status != Status::Ok) return "";
    if (ValidVersion(state.seen)) return state.seen;
    if (state.seen == LegacyId) return LegacyVersion;
    for (const auto& e : history) if (!e.id.empty() && e.id == state.seen) return e.version;
    return "";
}
enum class Decision { Hidden, Show, Baseline };   // Baseline: record this version as seen, silently (first install)
inline const char* DecisionText(Decision d) { return d == Decision::Show ? "show" : d == Decision::Baseline ? "baseline" : "hidden"; }
// `startupInstalled`: the engine reported Installed at start (the helper-launched process, once).
// `current`: the running version (« M.m.B »; anything else, such as « dev », never shows).
// `history`: History(). `card` receives what to show on Decision::Show (Unseen: id = current).
inline Decision Decide(bool startupInstalled, const Previous& previous, const std::string& current, const State& state,
                       const std::vector<Entry>& history, Content* card = nullptr) {
    if (card) *card = Content{};
    if (!startupInstalled || !ValidVersion(current)) return Decision::Hidden;
    if (state.status != Status::Ok && state.status != Status::Missing) return Decision::Hidden;   // never guess over a file we cannot trust
    const std::string seen = SeenVersion(state, history);
    if (!seen.empty() && CompareVersions(seen, current) >= 0) return Decision::Hidden;   // already seen up to here
    if (previous.kind == Previous::Kind::None) return state.status == Status::Missing ? Decision::Baseline : Decision::Hidden;
    const bool updated = (previous.kind == Previous::Kind::Installed && previous.version != current) || previous.kind == Previous::Kind::Portable;
    if (!updated) return Decision::Hidden;
    // Never shown a card here (or an id we cannot place): what the previous installed version had is not new.
    const std::string after = !seen.empty() ? seen : previous.kind == Previous::Kind::Installed ? previous.version : std::string();
    Content c = Unseen(history, after, current);
    if (c.Empty()) return Decision::Hidden;   // no notes since then: the generic notification, never old notes
    if (card) *card = std::move(c);
    return Decision::Show;
}

} // namespace ReleaseNotes
