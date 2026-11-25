#pragma once
// « Nouveautés » release notes (Paper p-L-0: 142E-0 Mini, 144O-0 Full opened). Model and decisions
// only; the modal draws it (src/modal.h, Spec::notes) and the host wires it (src/elements-integration.h).
//
// Contract (design/release-notes-modal-report.md, revised by design/release-notes-fix-report.md):
//   * The notes are compiled into the build: nothing is fetched. Their text lives in
//     release-notes/notes.json (FR + EN, written for each release, release-notes/README.md);
//     tools/release/build.py checks it and generates <out>-obj/gen/release-notes-data.h, included below.
//     A build without that header (test hosts, unstamped builds) has no notes.
//   * Content::id names THIS text (a hash computed by tools/release/release_notes.py): new text, new id.
//     A release that does not write new notes keeps the same id, so its update never shows the old
//     notes again as « new »: the generic « Mise à jour installée » notification is shown instead.
//   * Automatic Mini, once: only in the process the update helper launched after a successful
//     update (UpdateCore engine view == Installed at start, which happens exactly once per install)
//     AND <installRoot>\previous records a different installed version or a portable copy, AND the
//     build has notes, AND the read state has not seen their id. A first install (no `previous`), a
//     repair of the same version, a dev/portable run, another window or a normal launch never get it.
//   * Read state: <global data root>\release-notes « browser-release-notes 1 » / « seen=<id> »,
//     written atomically only once the modal has REALLY been shown. An unreadable file, a future
//     format or a damaged one is never overwritten and never triggers the modal.
//   * Manual: À propos › « Notes de version » opens the Full view (never writes the state); a build
//     without notes opens the release's GitHub page instead.
#include <string>
#include <vector>
#include "i18n.h"
#include "profile-store.h"
#if defined(BROWSER_HAS_BUILD_INFO) && defined(__has_include)
#if __has_include("release-notes-data.h")
#include "release-notes-data.h"   // generated from release-notes/notes.json (tools/release/release_notes.py)
#endif
#endif

namespace ReleaseNotes {

enum class Kind { Added, Fixed };
struct Section { Kind kind = Kind::Added; std::wstring title; std::vector<std::wstring> items; };

inline bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    return true;
}

// The notes of one release: each line in French and English (release-notes/notes.json).
struct Line { const wchar_t* fr = nullptr; const wchar_t* en = nullptr; };
struct Content {
    std::string id;                       // « notes-<hash> »; empty: this build has no notes
    std::vector<Line> added, fixed;       // « Ajouts » / « Corrections »; an empty one is not drawn
    bool Empty() const { return !ValidId(id) || (added.empty() && fixed.empty()); }
};
namespace Detail {
inline void Append(std::vector<Line>& out, const Line* lines) { for (; lines && lines->fr && lines->en; ++lines) out.push_back(*lines); }
}
// The build's own notes (empty when the build has none).
inline const Content& Compiled() {
    static const Content content = [] {
        Content c;
#ifdef BROWSER_RELEASE_NOTES_ID
        static const Line added[] = {BROWSER_RELEASE_NOTES_ADDED {nullptr, nullptr}};
        static const Line fixed[] = {BROWSER_RELEASE_NOTES_FIXED {nullptr, nullptr}};
        c.id = BROWSER_RELEASE_NOTES_ID;
        Detail::Append(c.added, added);
        Detail::Append(c.fixed, fixed);
#endif
        return c;
    }();
    return content;
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
inline bool ValidVersion(const std::string& v) {   // M.m.B, digits only, each part 1..5 digits
    int parts = 0; std::size_t run = 0;
    for (char c : v) {
        if (c >= '0' && c <= '9') { if (++run > 5) return false; }
        else if (c == '.') { if (!run) return false; ++parts; run = 0; }
        else return false;
    }
    return run && parts == 2;
}
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
enum class Decision { Hidden, Show, Baseline };   // Baseline: record the notes' id as seen, silently (first install)
inline const char* DecisionText(Decision d) { return d == Decision::Show ? "show" : d == Decision::Baseline ? "baseline" : "hidden"; }
// `startupInstalled`: the engine reported Installed at start (the helper-launched process, once).
// `current`: the running version (« M.m.B »; anything else, such as « dev », never shows).
// `notesId`: Compiled().id, empty when the build has no notes (never shows: the generic notification stays).
inline Decision Decide(bool startupInstalled, const Previous& previous, const std::string& current, const State& state, const std::string& notesId) {
    if (!startupInstalled || !ValidVersion(current) || !ValidId(notesId)) return Decision::Hidden;
    if (state.status != Status::Ok && state.status != Status::Missing) return Decision::Hidden;   // never guess over a file we cannot trust
    if (state.status == Status::Ok && state.seen == notesId) return Decision::Hidden;
    const bool updated = (previous.kind == Previous::Kind::Installed && previous.version != current) || previous.kind == Previous::Kind::Portable;
    if (updated) return Decision::Show;
    return state.status == Status::Missing && previous.kind == Previous::Kind::None ? Decision::Baseline : Decision::Hidden;
}

} // namespace ReleaseNotes
