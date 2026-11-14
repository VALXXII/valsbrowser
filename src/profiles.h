#pragma once
// Profiles (Paper p-I-0, design/profiles-implementation-plan.md): several local profiles, one
// profile per window. Data model, storage and lifecycle only; no UI. Pure Win32 + C++17.
//
// Layout under the data root (`dataDir` resolved by ResolveBrowserPaths):
//   <root>\profiles.txt                 registry « browser-profiles 1 », written only through
//                                       ProfileStore::CommitWith under ProfileStore::Lock(root)
//   default profile « Personnel »       the EXISTING layout, untouched: <root>\profile (WebView2),
//                                       <root>\sessions, <root>\settings, … (zero migration)
//   <root>\profiles\<id>\               any other profile, same sub-layout, its own WebView2 folder
//   <root>\profiles\.trash\<tomb>\      a deleted profile set aside for the 10 s undo: journal.txt
//                                       + data\ (what was moved). Nothing is ever deleted elsewhere.
//
// Window sessions (window-sessions.h) are keyed by the profile's data dir, so per-profile windows,
// orphan adoption and --restore-window work unchanged. Lock order: the root lock is always taken
// before a profile's own lock (for the default profile they are the same mutex, re-entrant).
//
// Delete is crash-safe: a journal is written (state « moving ») BEFORE anything moves and lists
// every move; at any later start PurgeTrash() rolls an interrupted « moving » or « restoring »
// back to a consistent state, keeping the profile's metadata, or purges a completed set-aside
// whose undo window is over (its owner process gone or its deadline passed).
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include "profile-store.h"
#include "window-sessions.h"
#include "session.h"
#include "profile-color.h"
#include "profile-cache.h"

namespace Profiles {

inline constexpr const wchar_t* DefaultId = L"default";
inline constexpr const wchar_t* DefaultName = L"Personnel";
inline constexpr const wchar_t* DefaultIcon = L"house";
inline constexpr std::size_t MaxNameLength = 32;     // user-perceived characters (surrogate pairs count once)
inline constexpr DWORD UndoMilliseconds = 10000;     // Paper P04: « Vous pourrez annuler pendant 10 secondes »
inline constexpr const char* Header = "browser-profiles 1";
inline constexpr const wchar_t* WindowClass = L"LisiereBrowser";
// The 12 Phosphor Fill identity icons (Paper P02/P03), in picker order.
inline const std::vector<std::wstring>& Icons() {
    static const std::vector<std::wstring> icons{L"briefcase", L"house", L"users-three", L"heart", L"graduation-cap", L"game-controller",
                                                 L"code", L"paint-brush", L"airplane-tilt", L"leaf", L"bicycle", L"handshake"};
    return icons;
}

struct Profile {
    std::wstring id, name, icon;
    int hue = 0;              // 0..359
    float intensity = .14f;   // OKLCH chroma 0..ProfileColor::MaxChroma (0.16)
    bool neutral = false;     // the grey « Personnel » look (intensity ignored)
    bool operator==(const Profile& o) const { return id == o.id && name == o.name && icon == o.icon && hue == o.hue && std::lround(intensity * 1000) == std::lround(o.intensity * 1000) && neutral == o.neutral; }
    bool operator!=(const Profile& o) const { return !(*this == o); }
};
// Snapshot of the registry. `items` order = display order (default first when it exists).
// Keep the Registry you loaded and pass it back to Save/Create/Delete: its private base makes the
// three-way merge with other windows' changes exact.
struct Registry {
    std::vector<Profile> items;
    std::wstring last;          // last used profile (start without --profile opens it)
    bool readOnly = false;      // unknown future format or unreadable: shown, never written (fail closed)
    std::wstring error;         // why readOnly
    const Profile* Find(const std::wstring& id) const { for (const auto& p : items) if (p.id == id) return &p; return nullptr; }
    // Internal three-way base (what this window last read or wrote).
    std::vector<Profile> baseItems; std::wstring baseLast;
};

struct Paths { std::wstring dataDir, profileDir, sessionFile, downloadsFile, settingsFile, logFile; };

enum class NameError { None, Empty, TooLong, Duplicate };

// A deleted profile set aside: undo with Undo(), confirm with Finalize() (or let PurgeTrash do it
// once `hold` is gone). `hold` is a named mutex owned by the deleting process for the undo window:
// while it lives, no process ever purges the tomb, even past `deadline` (the countdown can pause).
// Several tombs may be pending at once; undoing them in any order restores the original list order.
struct Tombstone {
    Profile profile; std::wstring trashDir; std::vector<std::wstring> closedSessions;
    int index = 0; ULONGLONG deadline = 0; HANDLE hold = nullptr;
};

// One open workspace of a profile. `hwnd` is set only when its sidecar validated (pid, class, token);
// `state`/`token` come from that validated sidecar (Active for « browser-window 1 » and untrusted ones).
struct WindowInfo {
    std::wstring sessionId; HWND hwnd = nullptr;
    std::vector<std::wstring> titles, urls; int active = 0; FILETIME lastActive{};
    bool cached = false; ULONG_PTR token = 0;
};

// ---------------------------------------------------------------------------------------------
namespace Detail {
inline std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr); return s;
}
inline std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n); return w;
}
// '%', '|', '=' and control characters escaped as %XX (UTF-8 bytes) so a name never breaks a record.
inline std::string Escape(const std::wstring& w) {
    std::string out; for (unsigned char c : Utf8(w)) { if (c < 0x20 || c == '%' || c == '|' || c == 0x7F) { char t[4]; snprintf(t, 4, "%%%02X", c); out += t; } else out += static_cast<char>(c); }
    return out;
}
inline bool Unescape(const std::string& s, std::wstring& out) {
    std::string bytes;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') { bytes += s[i]; continue; }
        if (i + 2 >= s.size() || !isxdigit(static_cast<unsigned char>(s[i + 1])) || !isxdigit(static_cast<unsigned char>(s[i + 2]))) return false;
        bytes += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)); i += 2;
    }
    out = Wide(bytes); return bytes.empty() || !out.empty();
}
inline std::vector<std::string> Split(const std::string& s, char sep) { std::vector<std::string> v; std::size_t a = 0, b; while ((b = s.find(sep, a)) != std::string::npos) { v.push_back(s.substr(a, b - a)); a = b + 1; } v.push_back(s.substr(a)); return v; }
inline bool ParseInt(const std::string& s, int lo, int hi, int& out) {
    if (s.empty() || s.size() > 6) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    const int v = std::stoi(s); if (v < lo || v > hi) return false; out = v; return true;
}
inline bool IconKnown(const std::wstring& icon) { const auto& i = Icons(); return std::find(i.begin(), i.end(), icon) != i.end(); }
inline std::wstring Lower(std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(towlower(c)); return s; }
inline bool StartsWithI(const std::wstring& s, const std::wstring& prefix) { return s.size() >= prefix.size() && Lower(s.substr(0, prefix.size())) == Lower(prefix); }
inline std::wstring Full(const std::wstring& p) {
    wchar_t buf[MAX_PATH * 4]; const DWORD n = GetFullPathNameW(p.c_str(), static_cast<DWORD>(std::size(buf)), buf, nullptr);
    std::wstring r = (n > 0 && n < std::size(buf)) ? std::wstring(buf, n) : p;
    while (r.size() > 3 && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    return r;
}
inline bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// Registry document for ProfileStore::CommitWith.
struct Doc {
    std::vector<Profile> items; std::wstring last;
    bool operator==(const Doc& o) const { return items == o.items && last == o.last; }
};
inline void Write(std::ostream& out, const Doc& d) {
    out << Header << "\n";
    if (!d.last.empty()) out << "last=" << Utf8(d.last) << "\n";
    for (const auto& p : d.items)
        out << "p=" << Utf8(p.id) << "|" << Utf8(p.icon) << "|" << p.hue << "|" << std::lround(std::clamp(p.intensity, 0.f, ProfileColor::MaxChroma) * 1000) << "|" << (p.neutral ? 1 : 0) << "|" << Escape(p.name) << "\n";
}
inline bool ValidId(const std::wstring& id);
inline Doc Read(std::istream& in, bool* damaged) {
    Doc d; std::string line; bool first = true, bad = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) { first = false; continue; }
        if (line.empty()) continue;
        if (line.rfind("last=", 0) == 0) { const auto id = Wide(line.substr(5)); if (ValidId(id) && d.last.empty()) d.last = id; else bad = true; continue; }
        if (line.rfind("p=", 0) != 0) { bad = true; continue; }
        const auto f = Split(line.substr(2), '|');
        Profile p; int milli = 0, neutral = 0;
        if (f.size() != 6 || !ValidId(p.id = Wide(f[0])) || !IconKnown(p.icon = Wide(f[1])) || !ParseInt(f[2], 0, 359, p.hue) || !ParseInt(f[3], 0, 160, milli) || !ParseInt(f[4], 0, 1, neutral) || !Unescape(f[5], p.name) || p.name.empty()) { bad = true; continue; }
        bool dup = false; for (const auto& q : d.items) dup = dup || q.id == p.id;
        if (dup) { bad = true; continue; }
        p.intensity = milli / 1000.f; p.neutral = neutral != 0;
        d.items.push_back(std::move(p));
    }
    if (damaged) *damaged = bad;
    return d;
}
// Three-way merge by profile id: what this window changed since its base wins, everything else
// is the file's current state. A profile deleted on either side stays deleted.
inline Doc Merge(const Doc& base, const Doc& mine, const Doc& theirs) {
    auto find = [](const std::vector<Profile>& v, const std::wstring& id) -> const Profile* { for (const auto& p : v) if (p.id == id) return &p; return nullptr; };
    Doc out = theirs;
    for (const auto& b : base.items) if (!find(mine.items, b.id)) out.items.erase(std::remove_if(out.items.begin(), out.items.end(), [&](const Profile& p) { return p.id == b.id; }), out.items.end());
    for (const auto& m : mine.items) {
        const Profile* b = find(base.items, m.id);
        auto it = std::find_if(out.items.begin(), out.items.end(), [&](const Profile& p) { return p.id == m.id; });
        if (!b) {   // created (or restored) here: placed after its predecessor in this window's order
            if (it != out.items.end()) { *it = m; continue; }
            auto pos = out.items.begin();
            for (auto mi = mine.items.begin(); mi != mine.items.end() && mi->id != m.id; ++mi) {
                auto prev = std::find_if(out.items.begin(), out.items.end(), [&](const Profile& p) { return p.id == mi->id; });
                if (prev != out.items.end()) pos = prev + 1;
            }
            out.items.insert(pos, m);
        }
        else if (*b != m && it != out.items.end()) *it = m;                              // edited here (unless deleted there)
    }
    if (mine.last != base.last) out.last = mine.last;
    if (!out.last.empty() && !find(out.items, out.last)) out.last.clear();
    return out;
}
inline std::wstring RegistryFile(const std::wstring& root) { return root + L"\\profiles.txt"; }
inline std::wstring ProfilesDir(const std::wstring& root) { return root + L"\\profiles"; }
inline std::wstring TrashRoot(const std::wstring& root) { return root + L"\\profiles\\.trash"; }
} // namespace Detail

// Profile ids: « default », or lower-case letters, digits and '-' (2..40). Never a path.
inline bool Detail::ValidId(const std::wstring& id) {
    if (id.size() < 2 || id.size() > 40 || id.front() == L'-' || id.front() == L'.') return false;
    for (wchar_t c : id) if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-')) return false;
    return true;
}
inline bool ValidId(const std::wstring& id) { return Detail::ValidId(id); }
// One of the 12 identity icons of Icons() (public form of the registry's own validation).
inline bool IconKnown(const std::wstring& icon) { return Detail::IconKnown(icon); }

// ---- Paths ----------------------------------------------------------------------------------
// Default: the legacy layout at the root (logFile empty = keep the build's own log path).
inline Paths ResolvePaths(const std::wstring& root, const std::wstring& id) {
    Paths p;
    if (id == DefaultId || !ValidId(id)) { p.dataDir = root; p.profileDir = root + L"\\profile"; p.sessionFile = root + L"\\session"; p.downloadsFile = root + L"\\downloads"; p.settingsFile = root + L"\\settings"; return p; }
    p.dataDir = Detail::ProfilesDir(root) + L"\\" + id;
    p.profileDir = p.dataDir + L"\\profile"; p.sessionFile = p.dataDir + L"\\session"; p.downloadsFile = p.dataDir + L"\\downloads";
    p.settingsFile = p.dataDir + L"\\settings"; p.logFile = p.dataDir + L"\\browser.log";
    return p;
}
// The default profile's own entries at the root (base names; « name.* » backups/temp files go
// with them). Everything else at the root (executable, assets, logs, registry, other profiles)
// is never touched by a delete of the default profile. Keep in sync with every dataDir+L"\\…"
// in src (checked by tests/profiles_test.cpp).
inline const std::vector<std::wstring>& DefaultEntries() {
    static const std::vector<std::wstring> e{L"profile", L"session", L"sessions", L"history", L"bookmarks", L"settings", L"downloads", L"passwords", L"site-volumes", L"adblock", L"favicons", L"engine-args"};
    return e;
}

// ---- Names ----------------------------------------------------------------------------------
inline std::wstring TrimName(const std::wstring& name) {
    std::size_t a = 0, b = name.size();
    while (a < b && (iswspace(name[a]) || name[a] == 0x00A0)) ++a;
    while (b > a && (iswspace(name[b - 1]) || name[b - 1] == 0x00A0)) --b;
    std::wstring t = name.substr(a, b - a);
    for (auto& c : t) if (c < 0x20 || c == 0x7F) c = L' ';   // no control characters in a name
    return t;
}
inline std::size_t NameLength(const std::wstring& name) {
    std::size_t n = 0; for (std::size_t i = 0; i < name.size(); ++i) { ++n; if (IS_HIGH_SURROGATE(name[i]) && i + 1 < name.size() && IS_LOW_SURROGATE(name[i + 1])) ++i; }
    return n;
}
// Case- and accent-insensitive equality (« Travail » = « travail », « Été » = « Ete »).
inline bool SameName(const std::wstring& a, const std::wstring& b) {
    const std::wstring x = TrimName(a), y = TrimName(b);
    const int r = CompareStringEx(LOCALE_NAME_INVARIANT, LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC | NORM_IGNOREWIDTH, x.c_str(), static_cast<int>(x.size()), y.c_str(), static_cast<int>(y.size()), nullptr, nullptr, 0);
    return r == CSTR_EQUAL;
}
inline NameError ValidateName(const Registry& reg, const std::wstring& name, const std::wstring& exceptId) {
    const std::wstring t = TrimName(name);
    if (t.empty()) return NameError::Empty;
    if (NameLength(t) > MaxNameLength) return NameError::TooLong;
    for (const auto& p : reg.items) if (p.id != exceptId && SameName(p.name, t)) return NameError::Duplicate;
    return NameError::None;
}
inline std::wstring UniqueDefaultName(const Registry& reg) {
    for (int n = 1;; ++n) {
        const std::wstring candidate = n == 1 ? std::wstring(L"Nouveau profil") : L"Nouveau profil " + std::to_wstring(n);
        if (ValidateName(reg, candidate, L"") == NameError::None) return candidate;
    }
}
// Defaults for a new profile (Paper P03): first icon not used yet, hue furthest from the others.
inline std::wstring FirstUnusedIcon(const Registry& reg) {
    for (const auto& icon : Icons()) { bool used = false; for (const auto& p : reg.items) used = used || p.icon == icon; if (!used) return icon; }
    return Icons().front();
}
inline int FurthestHue(const Registry& reg) {
    std::vector<int> taken; for (const auto& p : reg.items) if (!p.neutral) taken.push_back(p.hue);
    if (taken.empty()) return 200;
    int best = 0, bestDistance = -1;
    for (int h = 0; h < 360; h += 5) {
        int d = 360; for (int t : taken) { int x = std::abs(h - t) % 360; d = std::min(d, std::min(x, 360 - x)); }
        if (d > bestDistance) { bestDistance = d; best = h; }
    }
    return best;
}

// ---- Registry -------------------------------------------------------------------------------
namespace Detail {
inline Profile DefaultProfile() { Profile p; p.id = DefaultId; p.name = DefaultName; p.icon = DefaultIcon; p.hue = 0; p.intensity = 0; p.neutral = true; return p; }
inline std::wstring NewId() {
    static unsigned counter = 0; LARGE_INTEGER qpc; QueryPerformanceCounter(&qpc);
    const unsigned long long x = (static_cast<unsigned long long>(qpc.QuadPart) * 0x9E3779B97F4A7C15ull) ^ (static_cast<unsigned long long>(GetCurrentProcessId()) << 32) ^ (++counter * 0xBF58476D1CE4E5B9ull);
    wchar_t t[24]; swprintf(t, 24, L"p%012llx", x & 0xFFFFFFFFFFFFull); return t;
}
// Profile folders present on disk but missing from the registry (registry lost or damaged): they
// come back as « Profil N » so no profile's data is ever orphaned silently. Caller holds the lock.
inline void AdoptFolders(const std::wstring& root, Doc& d) {
    std::error_code ec; int n = 2;
    for (const auto& e : std::filesystem::directory_iterator(ProfilesDir(root), ec)) {
        const std::wstring id = e.path().filename().wstring();
        if (!e.is_directory(ec) || !ValidId(id) || id == DefaultId) continue;
        bool known = false; for (const auto& p : d.items) known = known || p.id == id;
        if (known) continue;
        Profile p; p.id = id; p.icon = Icons().front(); p.hue = 200; p.intensity = .14f;
        Registry tmp; tmp.items = d.items;
        do { p.name = L"Profil " + std::to_wstring(n++); } while (ValidateName(tmp, p.name, L"") != NameError::None);
        d.items.push_back(p);
    }
}
inline Registry ToRegistry(const Doc& d, const Doc& base) { Registry r; r.items = d.items; r.last = d.last; r.baseItems = base.items; r.baseLast = base.last; return r; }
inline ProfileStore::Result Commit(const std::wstring& root, Doc& base, Doc& mine) {
    return ProfileStore::CommitWith<Doc>(root, RegistryFile(root), base, mine, [](std::istream& in, bool* d) { return Read(in, d); },
        [](std::ostream& out, const Doc& d) { Write(out, d); }, [](const Doc& b, const Doc& m, const Doc& t) { return Merge(b, m, t); }, 5000, {Header});
}
} // namespace Detail
inline void PurgeTrash(const std::wstring& root);

// Reads the registry. First run (no profiles.txt): creates it with the default « Personnel »
// (house, neutral) plus any profile folder found — nothing on disk is moved. An unknown format
// or an unreadable file gives a read-only snapshot (never overwritten); if it has no profile at
// all, the default is shown so the browser still starts on the legacy data.
inline Registry Load(const std::wstring& root) {
    PurgeTrash(root);   // finishes or rolls back any interrupted delete first
    std::string bytes; bool readOk = false, exists = false;
    { ProfileStore::Lock lock(root, 10000); readOk = ProfileStore::ReadAll(Detail::RegistryFile(root), bytes); exists = Detail::Exists(Detail::RegistryFile(root)); }
    if (!readOk) { Registry r; r.items = {Detail::DefaultProfile()}; r.readOnly = true; r.error = L"unreadable"; return r; }
    if (!bytes.empty() && ProfileStore::FirstLine(bytes) != Header) { Registry r; r.items = {Detail::DefaultProfile()}; r.readOnly = true; r.error = L"unknown-format"; return r; }
    std::istringstream in(bytes); bool damaged = false;
    Detail::Doc doc = Detail::Read(in, &damaged);
    if (!damaged && !bytes.empty() && ProfileStore::RecordLines(bytes) != doc.items.size() + (doc.last.empty() ? 0 : 1)) damaged = true;
    const bool firstRun = !exists || bytes.empty();
    Detail::Doc mine = doc;
    if (firstRun) mine.items.insert(mine.items.begin(), Detail::DefaultProfile());
    { ProfileStore::Lock lock(root, 10000); if (lock.Held()) Detail::AdoptFolders(root, mine); }
    if (firstRun || damaged || !(mine == doc)) {
        Detail::Doc base = firstRun ? Detail::Doc{} : doc;
        if (damaged) base = doc;   // CommitWith backs the damaged file up byte for byte before writing
        const auto r = Detail::Commit(root, base, mine);
        if (r == ProfileStore::Result::UnknownFormat || r == ProfileStore::Result::Failed || r == ProfileStore::Result::Busy) {
            Registry reg = Detail::ToRegistry(mine.items.empty() ? Detail::Doc{{Detail::DefaultProfile()}, L""} : mine, doc);
            reg.readOnly = true; reg.error = r == ProfileStore::Result::Busy ? L"busy" : L"write-failed"; return reg;
        }
        return Detail::ToRegistry(mine, mine);
    }
    if (doc.items.empty()) { Registry reg = Detail::ToRegistry(Detail::Doc{{Detail::DefaultProfile()}, L""}, doc); reg.readOnly = true; reg.error = L"empty"; return reg; }
    return Detail::ToRegistry(doc, doc);
}
// Writes `reg` (three-way merged with the file's current state); `reg` becomes the merged result.
inline bool Save(const std::wstring& root, Registry& reg) {
    if (reg.readOnly) return false;
    Detail::Doc base{reg.baseItems, reg.baseLast}, mine{reg.items, reg.last};
    const auto r = Detail::Commit(root, base, mine);
    if (r != ProfileStore::Result::Written && r != ProfileStore::Result::Unchanged) return false;
    reg.items = mine.items; reg.last = mine.last; reg.baseItems = base.items; reg.baseLast = base.last;
    return true;
}
inline bool Save(const std::wstring& root, const Registry& reg) { Registry copy = reg; return Save(root, copy); }
// Marks `id` as the last used profile (start without --profile opens it).
inline bool SetLast(const std::wstring& root, Registry& reg, const std::wstring& id) {
    if (!reg.Find(id) || reg.last == id) return reg.Find(id) != nullptr;
    reg.last = id; return Save(root, reg);
}
// Profile a window should open: `requested` if it exists, else the last used, else the default,
// else the first. Empty only if the registry is empty.
inline std::wstring Active(const Registry& reg, const std::wstring& requested) {
    if (!requested.empty() && reg.Find(requested)) return requested;
    if (!reg.last.empty() && reg.Find(reg.last)) return reg.last;
    if (reg.Find(DefaultId)) return DefaultId;
    return reg.items.empty() ? std::wstring() : reg.items.front().id;
}

// Creates a profile: new id, its folder, then the registry record (same lock). Name, icon and
// colour come from `seed` (validated; an empty name becomes UniqueDefaultName). Returns the
// created profile, or a profile with an empty id on failure (invalid name, read-only registry,
// lock busy or disk error; nothing is left behind).
inline Profile Create(const std::wstring& root, Registry& reg, Profile seed, NameError* nameError = nullptr) {
    if (nameError) *nameError = NameError::None;
    if (reg.readOnly) return {};
    seed.name = TrimName(seed.name); if (seed.name.empty()) seed.name = UniqueDefaultName(reg);
    if (const auto e = ValidateName(reg, seed.name, L""); e != NameError::None) { if (nameError) *nameError = e; return {}; }
    if (!Detail::IconKnown(seed.icon)) seed.icon = FirstUnusedIcon(reg);
    seed.hue = ((seed.hue % 360) + 360) % 360; seed.intensity = std::clamp(seed.intensity, 0.f, ProfileColor::MaxChroma);
    ProfileStore::Lock lock(root, 10000); if (!lock.Held()) return {};
    std::wstring id; for (int attempt = 0; attempt < 20; ++attempt) { id = Detail::NewId(); if (!Detail::Exists(ResolvePaths(root, id).dataDir) && !reg.Find(id)) break; id.clear(); }
    if (id.empty()) return {};
    const auto paths = ResolvePaths(root, id);
    std::error_code ec; std::filesystem::create_directories(Detail::ProfilesDir(root), ec);
    if (!CreateDirectoryW(paths.dataDir.c_str(), nullptr)) return {};   // collision refused: never reuse an existing folder
    seed.id = id;
    Registry next = reg; next.items.push_back(seed);
    if (!Save(root, next) || !next.Find(id)) { RemoveDirectoryW(paths.dataDir.c_str()); return {}; }
    reg = next;
    return seed;
}
// Edits name/icon/colour of `p.id` (validated like Create).
inline bool Update(const std::wstring& root, Registry& reg, Profile p, NameError* nameError = nullptr) {
    if (nameError) *nameError = NameError::None;
    if (reg.readOnly || !reg.Find(p.id)) return false;
    p.name = TrimName(p.name);
    if (const auto e = ValidateName(reg, p.name, p.id); e != NameError::None) { if (nameError) *nameError = e; return false; }
    if (!Detail::IconKnown(p.icon)) return false;
    p.hue = ((p.hue % 360) + 360) % 360; p.intensity = std::clamp(p.intensity, 0.f, ProfileColor::MaxChroma);
    Registry next = reg; for (auto& q : next.items) if (q.id == p.id) q = p;
    if (!Save(root, next)) return false;
    reg = next; return true;
}

// ---- Windows --------------------------------------------------------------------------------
namespace Detail {
inline std::wstring SidecarFile(const std::wstring& dataDir, const std::wstring& sessionId) { return WindowSessions::Dir(dataDir) + L"\\" + sessionId + L".hwnd"; }
inline ULONG_PTR ProfileTag(const std::wstring& id) { std::uint64_t h = 1469598103934665603ull; for (wchar_t c : id) { h ^= c; h *= 1099511628211ull; } return static_cast<ULONG_PTR>(h | 1); }
inline const wchar_t* PropName() { return L"BrowserProfileTag"; }   // « browser-window 1 » sidecars (older builds) only
inline const wchar_t* TokenProp() { return L"BrowserWindowToken"; }
inline std::uint64_t Mix64(std::uint64_t x) { x += 0x9E3779B97F4A7C15ull; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull; x = (x ^ (x >> 27)) * 0x94D049BB133111EBull; return x ^ (x >> 31); }
} // namespace Detail

// ---- Same-window workspaces: one HWND, several profiles ----------------------------------------
// A window may host several profiles (« workspaces »): one ACTIVE (shown) and others CACHED (parked
// with live WebViews, or cold with only their session). Each workspace has its own session claim
// (WindowSessions Live mutex) in ITS profile's data dir and its own sidecar there, so every profile
// sees exactly the windows that hold it, and Delete keeps refusing while any of them does.
//
// Membership is proven by an opaque per-window token, not by a profile tag: the HWND carries ONE
// property BrowserWindowToken (random, minted once by the window's own process, never removed by
// publish/unpublish; it dies with the window), and each sidecar records pid + hwnd + token. A sidecar
// is trusted only if the HWND still exists, belongs to that pid, has our class and carries that
// token: a reused handle (other process, or a new window of the same process) never validates.
inline constexpr const wchar_t* PrivateWindowProperty = L"BrowserPrivateWindow";
inline bool IsPrivateWindow(HWND hwnd) { return hwnd && reinterpret_cast<ULONG_PTR>(GetPropW(hwnd, PrivateWindowProperty)) == 1; }
enum class WorkspaceState { Active, Cached };
// This window's token (minted on first call). 0: no window, or a window of another process (only the
// owning process mints or reads its own token here; validation reads the property directly).
inline ULONG_PTR WindowToken(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return 0;
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid); if (pid != GetCurrentProcessId()) return 0;
    if (const ULONG_PTR t = reinterpret_cast<ULONG_PTR>(GetPropW(hwnd, Detail::TokenProp()))) return t;
    LARGE_INTEGER q{}; QueryPerformanceCounter(&q); static std::uint64_t counter = 0;
    const std::uint64_t v = Detail::Mix64(static_cast<std::uint64_t>(q.QuadPart) ^ (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32)
                                         ^ static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hwnd)) ^ (++counter << 48) ^ GetTickCount64());
    const ULONG_PTR t = static_cast<ULONG_PTR>(v | 1);
    return SetPropW(hwnd, Detail::TokenProp(), reinterpret_cast<HANDLE>(t)) ? t : 0;
}
enum class PublishResult { Published, Invalid, Private, Failed };
// A workspace announces itself (after its session is claimed, on activation, park/unpark and when its
// tabs change): sidecar <its profile's sessions>\<sessionId>.hwnd (« browser-window 2 ») with pid,
// HWND, token, profile, state and tab titles/urls. Touches ONLY that file: publishing or unpublishing
// one workspace never changes another workspace's membership. A private window never publishes.
inline PublishResult PublishWorkspace(const Paths& paths, const std::wstring& profileId, const std::wstring& sessionId, HWND hwnd,
                                      const std::vector<std::wstring>& titles, const std::vector<std::wstring>& urls, int active, WorkspaceState state) {
    if (!hwnd || !WindowSessions::ValidId(sessionId) || !ValidId(profileId)) return PublishResult::Invalid;
    if (IsPrivateWindow(hwnd)) return PublishResult::Private;
    const ULONG_PTR token = WindowToken(hwnd); if (!token) return PublishResult::Invalid;
    FILETIME now; GetSystemTimeAsFileTime(&now);
    std::ostringstream o;
    o << "browser-window 2\npid=" << GetCurrentProcessId() << "\nhwnd=" << reinterpret_cast<std::uintptr_t>(hwnd) << "\ntoken=" << static_cast<unsigned long long>(token)
      << "\nprofile=" << Detail::Utf8(profileId) << "\nstate=" << (state == WorkspaceState::Active ? "active" : "cached")
      << "\nactive-time=" << ((static_cast<unsigned long long>(now.dwHighDateTime) << 32) | now.dwLowDateTime) << "\nactive=" << active << "\n";
    for (std::size_t i = 0; i < titles.size() || i < urls.size(); ++i)
        o << "tab=" << Detail::Escape(i < titles.size() ? titles[i] : L"") << "|" << Detail::Escape(i < urls.size() ? urls[i] : L"") << "\n";
    std::error_code ec; std::filesystem::create_directories(WindowSessions::Dir(paths.dataDir), ec);
    return ProfileStore::WriteAtomic(Detail::SidecarFile(paths.dataDir, sessionId), o.str()) ? PublishResult::Published : PublishResult::Failed;
}
inline bool PublishWindow(const Paths& paths, const std::wstring& profileId, const std::wstring& sessionId, HWND hwnd,
                          const std::vector<std::wstring>& titles = {}, const std::vector<std::wstring>& urls = {}, int active = 0, WorkspaceState state = WorkspaceState::Active) {
    return PublishWorkspace(paths, profileId, sessionId, hwnd, titles, urls, active, state) == PublishResult::Published;
}
// Removes that workspace's sidecar only. The window token and every other workspace stay. (`hwnd` is
// accepted for compatibility and ignored: no window property is removed any more.)
inline void UnpublishWindow(const Paths& paths, const std::wstring& sessionId, HWND hwnd = nullptr) {
    (void)hwnd;
    if (WindowSessions::ValidId(sessionId)) DeleteFileW(Detail::SidecarFile(paths.dataDir, sessionId).c_str());
}

// ---- Private windows ---------------------------------------------------------------------------
// A private window (--private) belongs to no profile and writes nothing: no registry, sidecar or
// session. It marks itself with the window property PrivateWindowProperty = 1 (set by the window's
// own process at start, removed on close). It still uses the default profile's WebView2 folder
// (<root>\profile, InPrivate), so deleting the default profile must wait until none is open.
// (PrivateWindowProperty / IsPrivateWindow are declared with the workspaces above.)
inline void MarkPrivateWindow(HWND hwnd) { if (hwnd) SetPropW(hwnd, PrivateWindowProperty, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1))); }
inline void UnmarkPrivateWindow(HWND hwnd) { if (hwnd) RemovePropW(hwnd, PrivateWindowProperty); }
// Open private windows (all processes; `except`: a window not to count). Checks each top-level window:
// still a window, our class, property exactly 1, and an owning process that is alive. Not scoped to
// one data root (the property carries no root): with two installs side by side it may count the
// other's private windows too, which only ever blocks a default-profile delete, never allows one.
inline int PrivateWindowCount(HWND except = nullptr) {
    struct Ctx { HWND except; int n; } ctx{except, 0};
    EnumWindows([](HWND h, LPARAM l) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(l); wchar_t cls[64] = {}; DWORD pid = 0;
        if (h == c.except || !IsWindow(h) || !GetClassNameW(h, cls, 64) || wcscmp(cls, WindowClass) != 0) return TRUE;
        if (reinterpret_cast<ULONG_PTR>(GetPropW(h, PrivateWindowProperty)) != 1 || !GetWindowThreadProcessId(h, &pid) || !pid) return TRUE;
        HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!p) return TRUE;
        const bool alive = WaitForSingleObject(p, 0) == WAIT_TIMEOUT; CloseHandle(p);
        if (alive) ++c.n;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.n;
}
// Session ids of the windows OPEN in a data dir. A window's session file may not exist yet (written
// on page ready / close) while its sidecar does, so ids come from both « <id> » session files and
// « <id>.hwnd » sidecars; the window's Live mutex alone decides that it is open. Sidecars with an
// invalid id or no Live mutex (crashed or closed window) are ignored. Deduplicated, no order.
inline std::vector<std::wstring> OpenSessionIds(const std::wstring& dataDir) {
    std::vector<std::wstring> ids;
    auto add = [&](const std::wstring& id) {
        if (!WindowSessions::ValidId(id) || std::find(ids.begin(), ids.end(), id) != ids.end()) return;
        if (WindowSessions::Live(dataDir, id)) ids.push_back(id);
    };
    for (const auto& e : WindowSessions::List(dataDir)) add(e.id);
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(WindowSessions::Dir(dataDir), ec)) {
        const std::wstring name = e.path().filename().wstring();
        if (name.size() > 5 && name.compare(name.size() - 5, 5, L".hwnd") == 0 && e.is_regular_file(ec)) add(name.substr(0, name.size() - 5));
    }
    return ids;
}
// Open windows of a profile, most recently active first. A window whose sidecar cannot be trusted
// (stale HWND, other process/class/profile, malformed) is still listed with hwnd null, so counts stay
// exact; its urls come from its session file when there is one.
inline std::vector<WindowInfo> LiveWindows(const std::wstring& root, const std::wstring& id) {
    std::vector<WindowInfo> out; if (!ValidId(id)) return out;
    const auto paths = ResolvePaths(root, id);
    for (const auto& sid : OpenSessionIds(paths.dataDir)) {
        const WindowSessions::Entry e{sid, {}};
        WindowInfo w; w.sessionId = e.id;
        std::string bytes;
        const bool read = ProfileStore::ReadAll(Detail::SidecarFile(paths.dataDir, e.id), bytes);
        const std::string head = read ? ProfileStore::FirstLine(bytes) : std::string();
        const bool v1 = head == "browser-window 1", v2 = head == "browser-window 2";
        if (v1 || v2) {
            std::istringstream in(bytes); std::string line; DWORD pid = 0; std::uintptr_t h = 0; std::wstring profile; unsigned long long t = 0, token = 0;
            std::vector<std::wstring> titles, urls; int active = 0; std::string state;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const auto eq = line.find('='); if (eq == std::string::npos) continue;
                const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
                try {
                    if (k == "pid") pid = static_cast<DWORD>(std::stoul(v)); else if (k == "hwnd") h = static_cast<std::uintptr_t>(std::stoull(v));
                    else if (k == "profile") profile = Detail::Wide(v); else if (k == "active-time") t = std::stoull(v); else if (k == "active") active = std::stoi(v);
                    else if (k == "token") token = std::stoull(v); else if (k == "state") state = v;
                    else if (k == "tab") { const auto f = Detail::Split(v, '|'); std::wstring a, b; if (f.size() == 2 && Detail::Unescape(f[0], a) && Detail::Unescape(f[1], b)) { titles.push_back(a); urls.push_back(b); } }
                } catch (...) {}
            }
            HWND hwnd = reinterpret_cast<HWND>(h); DWORD owner = 0; wchar_t cls[64] = {};
            const bool window = hwnd && IsWindow(hwnd) && GetWindowThreadProcessId(hwnd, &owner) && owner == pid && GetClassNameW(hwnd, cls, 64) && wcscmp(cls, WindowClass) == 0 && profile == id && !IsPrivateWindow(hwnd);   // a private window never hosts a profile
            const bool member = v2 ? token != 0 && (state == "active" || state == "cached") && reinterpret_cast<ULONG_PTR>(GetPropW(hwnd, Detail::TokenProp())) == static_cast<ULONG_PTR>(token)
                                   : reinterpret_cast<ULONG_PTR>(GetPropW(hwnd, Detail::PropName())) == Detail::ProfileTag(id);
            if (window && member) {
                w.hwnd = hwnd; w.titles = titles; w.urls = urls; w.active = active; w.lastActive.dwHighDateTime = static_cast<DWORD>(t >> 32); w.lastActive.dwLowDateTime = static_cast<DWORD>(t);
                w.cached = v2 && state == "cached"; w.token = v2 ? static_cast<ULONG_PTR>(token) : 0;
            }
        }
        if (w.urls.empty()) {
            std::string s; if (ProfileStore::ReadAll(WindowSessions::File(paths.dataDir, e.id), s)) { std::istringstream in(s); const auto st = BrowserSession::Read(in); for (const auto& u : st.urls) w.urls.push_back(Detail::Wide(u)); w.active = st.active; }
        }
        out.push_back(std::move(w));
    }
    std::stable_sort(out.begin(), out.end(), [](const WindowInfo& a, const WindowInfo& b) { return CompareFileTime(&a.lastActive, &b.lastActive) > 0; });
    return out;
}
// Every open workspace counts (active, parked or cold): each holds a session claim in the profile's folder.
inline int OpenWindowCount(const std::wstring& root, const std::wstring& id) { return static_cast<int>(LiveWindows(root, id).size()); }
// Validated windows where `id` is the shown workspace / a cached (parked or cold) workspace.
inline std::vector<WindowInfo> ActiveIn(const std::wstring& root, const std::wstring& id) { std::vector<WindowInfo> v; for (auto& w : LiveWindows(root, id)) if (w.hwnd && !w.cached) v.push_back(std::move(w)); return v; }
inline std::vector<WindowInfo> CachedIn(const std::wstring& root, const std::wstring& id) { std::vector<WindowInfo> v; for (auto& w : LiveWindows(root, id)) if (w.hwnd && w.cached) v.push_back(std::move(w)); return v; }

// Evict-profile request between windows (WM_COPYDATA « PROF » text: EvictCommand + id). The receiver
// disposes ITS workspace of that profile: saves its session, closes its controllers, releases its
// environment, waits for the browser process to exit, unpublishes and releases its claim with
// keep = true. If that workspace is the shown one it first switches to WorkspaceCache::Successor; if it
// is the window's only workspace the window closes as before. Other profiles in the window are never
// closed. Answer (the SendMessage result):
inline constexpr const wchar_t* EvictCommand = L"evict-profile ";
enum EvictAnswer : int { EvictNotApplicable = 0, EvictAccepted = 1, EvictBusy = 2 };

// ---- Window sets: a window's workspaces survive a restart in the SAME window -------------------------
// <root>\windows\<token hex> (« browser-window-set 1 »): one `slot=<profile>|<session>|active|cached`
// line per workspace of that window, exactly one active. Written atomically under the root lock on
// every switch / eviction / close (a private window never writes one). At start, whichever process
// adopts a session listed in a set (FindWindowSet) claims the set's other slots for itself as cold
// workspaces (ClaimSlot) and rewrites the set under its own token (RetireWindowSet the old file).
// Cached slots are never adopted alone nor relaunched as separate windows (AdoptWorkspace,
// ProfilesWithOrphans) as long as their set's active session still exists; a set whose active
// session is gone no longer holds anything, so its cached sessions become ordinary orphans (never lost).
// A malformed or unknown set file is ignored and never deleted (fail closed).
struct WindowSlot { std::wstring profile, session; WorkspaceState state = WorkspaceState::Cached; };
struct WindowSet { std::wstring token; std::vector<WindowSlot> slots; };
enum class SetResult { Written, Removed, Invalid, Private, Busy, Failed, InUse };
namespace Detail {
inline std::wstring WindowsDir(const std::wstring& root) { return root + L"\\windows"; }
inline std::wstring TokenHex(ULONG_PTR t) { wchar_t b[24]; swprintf(b, 24, L"%016llx", static_cast<unsigned long long>(t)); return b; }
inline bool ValidTokenHex(const std::wstring& s) { if (s.size() != 16) return false; for (wchar_t c : s) if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false; return s != L"0000000000000000"; }
inline bool ValidSlots(const std::vector<WindowSlot>& slots) {
    int actives = 0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!ValidId(slots[i].profile) || !WindowSessions::ValidId(slots[i].session)) return false;
        if (slots[i].state == WorkspaceState::Active) ++actives;
        for (std::size_t k = 0; k < i; ++k) if (slots[k].profile == slots[i].profile) return false;   // one workspace per profile per window
    }
    return actives == 1;
}
inline bool ParseWindowSet(const std::string& bytes, const std::wstring& token, WindowSet& out) {
    if (ProfileStore::FirstLine(bytes) != "browser-window-set 1") return false;
    std::istringstream in(bytes); std::string line; bool first = true; WindowSet s; s.token = token;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) { first = false; continue; }
        if (line.empty()) continue;
        if (line.rfind("slot=", 0) != 0) return false;   // unknown content: the whole file is ignored
        const auto f = Split(line.substr(5), '|'); if (f.size() != 3 || (f[2] != "active" && f[2] != "cached")) return false;
        s.slots.push_back({Wide(f[0]), Wide(f[1]), f[2] == "active" ? WorkspaceState::Active : WorkspaceState::Cached});
    }
    if (!ValidSlots(s.slots)) return false;
    out = std::move(s); return true;
}
inline bool SessionExists(const std::wstring& root, const WindowSlot& s) { return Exists(WindowSessions::File(ResolvePaths(root, s.profile).dataDir, s.session)); }
} // namespace Detail
// Every valid set (malformed ones skipped, never touched).
inline std::vector<WindowSet> ReadWindowSets(const std::wstring& root) {
    std::vector<WindowSet> out; std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(Detail::WindowsDir(root), ec)) {
        const std::wstring name = e.path().filename().wstring(); if (!e.is_regular_file(ec) || !Detail::ValidTokenHex(name)) continue;
        std::string bytes; WindowSet s; if (ProfileStore::ReadAll(e.path().wstring(), bytes) && Detail::ParseWindowSet(bytes, name, s)) out.push_back(std::move(s));
    }
    return out;
}
// Writes this window's set (replacing its previous content). Refused for a private window, an invalid
// slot list (ids, not exactly one active, a profile twice), a busy root lock or a disk error.
inline SetResult WriteWindowSet(const std::wstring& root, HWND hwnd, const std::vector<WindowSlot>& slots) {
    if (IsPrivateWindow(hwnd)) return SetResult::Private;
    const ULONG_PTR token = WindowToken(hwnd); if (!token || !Detail::ValidSlots(slots)) return SetResult::Invalid;
    std::ostringstream o; o << "browser-window-set 1\n";
    for (const auto& s : slots) o << "slot=" << Detail::Utf8(s.profile) << "|" << Detail::Utf8(s.session) << "|" << (s.state == WorkspaceState::Active ? "active" : "cached") << "\n";
    ProfileStore::Lock lock(root, 10000); if (!lock.Held()) return SetResult::Busy;
    std::error_code ec; std::filesystem::create_directories(Detail::WindowsDir(root), ec);
    return ProfileStore::WriteAtomic(Detail::WindowsDir(root) + L"\\" + Detail::TokenHex(token), o.str()) ? SetResult::Written : SetResult::Failed;
}
namespace Detail {
// A window of ANOTHER process still carries this token: that set is its live window's, not ours to retire.
inline bool TokenLiveElsewhere(const std::wstring& token) {
    struct Ctx { std::wstring token; bool found; } ctx{token, false};
    EnumWindows([](HWND h, LPARAM l) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(l); wchar_t cls[64] = {}; DWORD pid = 0;
        if (!GetClassNameW(h, cls, 64) || wcscmp(cls, WindowClass) != 0 || !GetWindowThreadProcessId(h, &pid) || pid == GetCurrentProcessId()) return TRUE;
        const ULONG_PTR t = reinterpret_cast<ULONG_PTR>(GetPropW(h, TokenProp()));
        if (t && TokenHex(t) == c.token) { c.found = true; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}
} // namespace Detail
// Removes a set by its token text: this window's own (WindowSetName(hwnd)) or one adopted from a window
// that is gone. Refused (InUse) while a window of another process still carries that token.
inline SetResult RetireWindowSet(const std::wstring& root, const std::wstring& token) {
    if (!Detail::ValidTokenHex(token)) return SetResult::Invalid;
    if (Detail::TokenLiveElsewhere(token)) return SetResult::InUse;
    ProfileStore::Lock lock(root, 10000); if (!lock.Held()) return SetResult::Busy;
    const std::wstring file = Detail::WindowsDir(root) + L"\\" + token;
    if (!Detail::Exists(file)) return SetResult::Removed;
    return DeleteFileW(file.c_str()) ? SetResult::Removed : SetResult::Failed;
}
inline std::wstring WindowSetName(HWND hwnd) { const ULONG_PTR t = WindowToken(hwnd); return t ? Detail::TokenHex(t) : std::wstring(); }
// The set listing (profile, session) — the one to restore into the window that adopted that session.
inline bool FindWindowSet(const std::wstring& root, const std::wstring& profile, const std::wstring& session, WindowSet& out) {
    for (auto& s : ReadWindowSets(root))
        for (const auto& slot : s.slots) if (slot.profile == profile && slot.session == session) { out = std::move(s); return true; }
    return false;
}
// Sessions of `profileId` held as CACHED slots by a set whose active session still exists: they come
// back inside that set's window only.
inline std::vector<std::wstring> CachedSlotSessions(const std::wstring& root, const std::wstring& profileId) {
    std::vector<std::wstring> out;
    for (const auto& s : ReadWindowSets(root)) {
        bool holds = false; for (const auto& slot : s.slots) if (slot.state == WorkspaceState::Active) holds = Detail::SessionExists(root, slot);
        if (!holds) continue;
        for (const auto& slot : s.slots) if (slot.state == WorkspaceState::Cached && slot.profile == profileId && std::find(out.begin(), out.end(), slot.session) == out.end()) out.push_back(slot.session);
    }
    return out;
}
// Claims a set's cached slot for this window (lazy restore: its session file must exist). Null when the
// slot is invalid, its file is gone, the profile lock is busy, or another process already holds it.
inline HANDLE ClaimSlot(const std::wstring& root, const WindowSlot& slot) {
    if (!ValidId(slot.profile) || !WindowSessions::ValidId(slot.session)) return nullptr;
    const auto dataDir = ResolvePaths(root, slot.profile).dataDir;
    ProfileStore::Lock lock(dataDir, 10000); if (!lock.Held()) return nullptr;
    if (!Detail::Exists(WindowSessions::File(dataDir, slot.session))) return nullptr;
    return WindowSessions::Claim(dataDir, slot.session);
}
// WindowSessions::Adopt for one profile workspace, set-aware: the sessions that are cached slots of a
// live set are neither adopted nor relaunched nor dropped (restoration off). `requested` is honoured as is.
inline WindowSessions::Adoption AdoptWorkspace(const std::wstring& root, const std::wstring& profileId, const std::wstring& legacyFile, const std::wstring& requested, bool restoreTabs) {
    WindowSessions::Adoption a; if (!ValidId(profileId)) return a;
    const auto dataDir = ResolvePaths(root, profileId).dataDir;
    const auto held = CachedSlotSessions(root, profileId);   // read before the profile lock (no nested locks)
    ProfileStore::Lock lock(dataDir, 10000);
    if (!lock.Held()) { while (!a.live) { a.id = WindowSessions::NewId(); a.live = WindowSessions::Claim(dataDir, a.id); } return a; }
    WindowSessions::MigrateLegacy(dataDir, legacyFile);
    WindowSessions::DropClosedLeftovers(dataDir);
    if (!requested.empty()) {
        if (WindowSessions::ValidId(requested) && (a.live = WindowSessions::Claim(dataDir, requested))) { a.id = requested; a.requested = true; }
    } else {
        for (const auto& id : WindowSessions::Orphans(dataDir)) {
            if (std::find(held.begin(), held.end(), id) != held.end()) continue;   // restored by its set's window
            if (!a.live) { if ((a.live = WindowSessions::Claim(dataDir, id))) { a.id = id; a.adopted = true; } continue; }
            if (!restoreTabs) { DeleteFileW(WindowSessions::File(dataDir, id).c_str()); continue; }
            if (HANDLE r = CreateMutexW(nullptr, FALSE, WindowSessions::ReserveName(dataDir, id).c_str())) { a.reservations.push_back(r); a.relaunch.push_back(id); }
        }
    }
    while (!a.live) { a.id = WindowSessions::NewId(); a.live = WindowSessions::Claim(dataDir, a.id); }
    return a;
}

// ---- Engine arguments shared by every environment on one user-data folder ------------------------------
// WebView2 refuses a second environment on the same user-data folder with different options
// (ERROR_INVALID_STATE): Travail running in window 1 and created later in window 2 must get the SAME
// arguments even if Travail's settings changed in between. <dataDir>\engine-args records the arguments
// in force and the processes holding an environment on it (pid + creation time; dead ones pruned).
// AcquireEngineArgs returns them while any OTHER live process holds them, else records `wanted` (derived
// by the caller from the profile's settings). So a changed engine flag applies when no process has an
// environment on that profile any more (every window of it, active or parked, released its environment;
// a cold workspace holds none). Call Release when this process releases that environment.
struct EngineLease { std::wstring arguments; bool shared = false; std::wstring error; };
namespace Detail {
inline std::wstring EngineArgsFile(const std::wstring& dataDir) { return dataDir + L"\\engine-args"; }
inline unsigned long long ProcessStart(HANDLE p) { FILETIME c{}, e{}, k{}, u{}; if (!GetProcessTimes(p, &c, &e, &k, &u)) return 0; return (static_cast<unsigned long long>(c.dwHighDateTime) << 32) | c.dwLowDateTime; }
inline bool HolderAlive(DWORD pid, unsigned long long start) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid); if (!p) return false;
    const bool alive = WaitForSingleObject(p, 0) == WAIT_TIMEOUT && ProcessStart(p) == start; CloseHandle(p); return alive;
}
struct EngineRecord { std::wstring arguments; std::vector<std::pair<DWORD, unsigned long long>> holders; };
inline bool ReadEngineRecord(const std::string& bytes, EngineRecord& r) {
    if (ProfileStore::FirstLine(bytes) != "browser-engine-args 1") return false;
    std::istringstream in(bytes); std::string line; std::getline(in, line);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            if (line.rfind("args=", 0) == 0) { std::wstring a; if (!Unescape(line.substr(5), a)) return false; r.arguments = a; }
            else if (line.rfind("holder=", 0) == 0) { const auto f = Split(line.substr(7), '|'); if (f.size() != 2) return false; r.holders.push_back({static_cast<DWORD>(std::stoul(f[0])), std::stoull(f[1])}); }
            else if (!line.empty()) return false;
        } catch (...) { return false; }
    }
    return true;
}
inline std::string WriteEngineRecord(const EngineRecord& r) {
    std::ostringstream o; o << "browser-engine-args 1\nargs=" << Escape(r.arguments) << "\n";
    for (const auto& h : r.holders) o << "holder=" << h.first << "|" << h.second << "\n";
    return o.str();
}
} // namespace Detail
inline EngineLease AcquireEngineArgs(const std::wstring& root, const std::wstring& id, const std::wstring& wanted) {
    EngineLease out; out.arguments = wanted;
    if (!ValidId(id)) { out.error = L"invalid"; return out; }
    const auto dataDir = ResolvePaths(root, id).dataDir;
    ProfileStore::Lock lock(dataDir, 10000); if (!lock.Held()) { out.error = L"busy"; return out; }
    std::string bytes; Detail::EngineRecord r; const DWORD self = GetCurrentProcessId(); const unsigned long long start = Detail::ProcessStart(GetCurrentProcess());
    if (ProfileStore::ReadAll(Detail::EngineArgsFile(dataDir), bytes) && !bytes.empty() && !Detail::ReadEngineRecord(bytes, r)) { out.error = L"unknown-format"; return out; }   // never overwritten
    std::vector<std::pair<DWORD, unsigned long long>> alive; bool others = false;
    for (const auto& h : r.holders) { if (h.first == self && h.second == start) continue; if (Detail::HolderAlive(h.first, h.second)) { alive.push_back(h); others = true; } }
    if (others) { out.arguments = r.arguments; out.shared = true; } else r.arguments = wanted;
    alive.push_back({self, start}); r.holders = alive;
    std::error_code ec; std::filesystem::create_directories(dataDir, ec);
    if (!ProfileStore::WriteAtomic(Detail::EngineArgsFile(dataDir), Detail::WriteEngineRecord(r))) out.error = L"write-failed";
    return out;
}
// This process no longer has an environment on `id`'s folder.
inline bool ReleaseEngineArgs(const std::wstring& root, const std::wstring& id) {
    if (!ValidId(id)) return false;
    const auto dataDir = ResolvePaths(root, id).dataDir;
    ProfileStore::Lock lock(dataDir, 10000); if (!lock.Held()) return false;
    std::string bytes; Detail::EngineRecord r;
    if (!ProfileStore::ReadAll(Detail::EngineArgsFile(dataDir), bytes) || bytes.empty()) return true;
    if (!Detail::ReadEngineRecord(bytes, r)) return false;
    const DWORD self = GetCurrentProcessId(); const unsigned long long start = Detail::ProcessStart(GetCurrentProcess());
    r.holders.erase(std::remove_if(r.holders.begin(), r.holders.end(), [&](const std::pair<DWORD, unsigned long long>& h) { return (h.first == self && h.second == start) || !Detail::HolderAlive(h.first, h.second); }), r.holders.end());
    return ProfileStore::WriteAtomic(Detail::EngineArgsFile(dataDir), Detail::WriteEngineRecord(r));
}

// Starts `exe --profile <id>` (plus `extra`, e.g. L"--restore-window <session>").
inline bool Launch(const std::wstring& exe, const std::wstring& id, const std::wstring& extra = L"") {
    if (!ValidId(id)) return false;
    std::wstring cmd = L"\"" + exe + L"\" --profile " + id; if (!extra.empty()) cmd += L" " + extra;
    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return true;
}
// Brings forward the most recently active window SHOWING `id`, else starts a new window of it. (Same-window
// switching does not use it: a click switches the window itself; « Ouvrir dans une nouvelle fenêtre » is Launch.)
inline bool FocusOrLaunch(const std::wstring& root, const std::wstring& id, const std::wstring& exe) {
    for (const auto& w : ActiveIn(root, id)) {
        if (!w.hwnd) continue;
        if (IsIconic(w.hwnd)) ShowWindow(w.hwnd, SW_RESTORE);
        AllowSetForegroundWindow(ASFW_ANY);
        SetForegroundWindow(w.hwnd); return true;
    }
    return Launch(exe, id);
}
// Cold start restore across profiles: the profiles other than `selfId` whose windows were left
// open (orphan sessions) when the browser closed or crashed; start one `Launch(exe, id)` each, it
// adopts the most recent orphan and relaunches the rest (WindowSessions::Adopt, per profile).
// Empty unless this is a cold start: if any window of any profile other than this one
// (`selfId`/`selfSession`) is open, another process already restored, so nothing is relaunched twice.
// Same-window workspaces: `self` lists every workspace of THIS window (active + cached slots it claimed);
// they never count as « another window is open ». Cached slots of live sets are not orphans to relaunch:
// they come back inside their set's window. Each listed profile is started with Launch(exe, id).
inline std::vector<std::wstring> ProfilesWithOrphans(const std::wstring& root, const Registry& reg, const std::vector<WindowSlot>& self) {
    std::vector<std::wstring> out;
    auto mine = [&](const std::wstring& id, const std::wstring& sid) { for (const auto& s : self) if (s.profile == id && s.session == sid) return true; return false; };
    for (const auto& p : reg.items) {
        const auto dataDir = ResolvePaths(root, p.id).dataDir;
        for (const auto& sid : OpenSessionIds(dataDir))
            if (!mine(p.id, sid)) return {};
    }
    for (const auto& p : reg.items) {
        bool here = false; for (const auto& s : self) here = here || (s.profile == p.id && s.state == WorkspaceState::Active);
        if (here) continue;
        const auto held = CachedSlotSessions(root, p.id);
        for (const auto& sid : WindowSessions::Orphans(ResolvePaths(root, p.id).dataDir))
            if (std::find(held.begin(), held.end(), sid) == held.end()) { out.push_back(p.id); break; }
    }
    return out;
}
inline std::vector<std::wstring> ProfilesWithOrphans(const std::wstring& root, const Registry& reg, const std::wstring& selfId, const std::wstring& selfSession) {
    return ProfilesWithOrphans(root, reg, std::vector<WindowSlot>{{selfId, selfSession, WorkspaceState::Active}});
}
// « Rouvrir ses fenêtres » after Undo: one process per closed session, each adopting its own tabs.
inline int ReopenSessions(const std::wstring& exe, const std::wstring& id, const std::vector<std::wstring>& sessions) {
    int n = 0; for (const auto& s : sessions) if (WindowSessions::ValidId(s) && Launch(exe, id, L"--restore-window " + s)) ++n;
    return n;
}

// ---- Delete / Undo / Purge -------------------------------------------------------------------
namespace Detail {
// Deletes a tree WITHOUT following junctions/symlinks (a reparse point is removed, not entered).
inline bool DeleteTree(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); return DeleteFileW(path.c_str()) != FALSE; }
    if (!(a & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW fd; HANDLE f = FindFirstFileW((path + L"\\*").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do { const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue; DeleteTree(path + L"\\" + n); } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path.c_str()) != FALSE;
}
// Code-side guard for every recursive deletion: an absolute, canonical path strictly inside
// <root>\profiles\.trash, whose own entry is a real directory (not a junction).
inline bool SafeTrashPath(const std::wstring& root, const std::wstring& path) {
    const std::wstring trash = Full(TrashRoot(root)), p = Full(path);
    if (p.size() < 3 || p[1] != L':' || p.find(L"\\..") != std::wstring::npos) return false;
    if (!StartsWithI(p, trash + L"\\") || p.size() <= trash.size() + 1) return false;
    if (p.find(L'\\', trash.size() + 1) != std::wstring::npos) return false;   // the tomb itself, never deeper or shallower
    const DWORD a = GetFileAttributesW(p.c_str());
    return a == INVALID_FILE_ATTRIBUTES || ((a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT));
}
struct Move { std::wstring from, to; };
// `order`: the list's ids at delete time, with the profiles still set aside by other pending deletes
// put back at their places (ghosts), so undos in ANY order rebuild the original order (Reinsert).
struct Journal { std::string state; Profile profile; int index = 0; ULONGLONG deadlineWall = 0; std::vector<Move> moves; std::vector<std::wstring> sessions; std::wstring holdName; std::vector<std::wstring> order; };
inline std::wstring JournalFile(const std::wstring& tomb) { return tomb + L"\\journal.txt"; }
// Journal paths are stored RELATIVE to the data root (« browser-profile-tomb 2 »), so a data root
// copied elsewhere (portable → installed migration, update-helper.cpp) keeps a journal that only
// ever moves files inside its own root. A path outside the root cannot be written or read back.
inline bool RelativeToRoot(const std::wstring& root, const std::wstring& path, std::wstring& rel) {
    const std::wstring r = Full(root), p = Full(path);
    if (!StartsWithI(p, r + L"\\") || p.size() <= r.size() + 1) return false;
    rel = p.substr(r.size() + 1); return true;
}
inline bool ResolveInRoot(const std::wstring& root, const std::wstring& rel, std::wstring& path) {
    if (rel.empty() || rel.find(L':') != std::wstring::npos || rel.front() == L'\\' || rel.front() == L'/' || rel.find(L'/') != std::wstring::npos) return false;
    for (const auto& part : Split(Utf8(rel), '\\')) if (part.empty() || part == "." || part == "..") return false;
    path = Full(root) + L"\\" + rel; return true;
}
inline bool WriteJournal(const std::wstring& root, const std::wstring& tomb, const Journal& j) {
    std::ostringstream o; o << "browser-profile-tomb 2\nstate=" << j.state << "\nindex=" << j.index << "\ndeadline=" << j.deadlineWall << "\nhold=" << Utf8(j.holdName) << "\n";
    Doc d; d.items = {j.profile}; std::ostringstream rec; Write(rec, d); std::string r = rec.str(); r = r.substr(r.find('\n') + 1); o << r;
    for (const auto& m : j.moves) { std::wstring from, to; if (!RelativeToRoot(root, m.from, from) || !RelativeToRoot(root, m.to, to)) return false; o << "move=" << Escape(from) << "|" << Escape(to) << "\n"; }
    for (const auto& s : j.sessions) o << "session=" << Utf8(s) << "\n";
    if (!j.order.empty()) { o << "order="; for (std::size_t i = 0; i < j.order.size(); ++i) o << (i ? "," : "") << Utf8(j.order[i]); o << "\n"; }   // ids: [a-z0-9-], no comma
    return ProfileStore::WriteAtomic(JournalFile(tomb), o.str());
}
inline bool ReadJournal(const std::wstring& root, const std::wstring& tomb, Journal& j) {
    std::string bytes; if (!ProfileStore::ReadAll(JournalFile(tomb), bytes)) return false;
    const std::string head = ProfileStore::FirstLine(bytes); const bool v2 = head == "browser-profile-tomb 2";
    if (!v2 && head != "browser-profile-tomb 1") return false;   // v1 (absolute paths): accepted only inside this root
    std::istringstream in(bytes); std::string line; std::string records = std::string(Header) + "\n";
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            if (line.rfind("state=", 0) == 0) j.state = line.substr(6);
            else if (line.rfind("index=", 0) == 0) j.index = std::stoi(line.substr(6));
            else if (line.rfind("deadline=", 0) == 0) j.deadlineWall = std::stoull(line.substr(9));
            else if (line.rfind("hold=", 0) == 0) j.holdName = Wide(line.substr(5));
            else if (line.rfind("p=", 0) == 0) records += line + "\n";
            else if (line.rfind("move=", 0) == 0) {
                const auto f = Split(line.substr(5), '|'); Move m; std::wstring a, b, rel;
                if (f.size() != 2 || !Unescape(f[0], a) || !Unescape(f[1], b)) return false;
                if (v2) { if (!ResolveInRoot(root, a, m.from) || !ResolveInRoot(root, b, m.to)) return false; }
                else { if (!RelativeToRoot(root, a, rel) || !RelativeToRoot(root, b, rel)) return false; m.from = a; m.to = b; }
                j.moves.push_back(m);
            }
            else if (line.rfind("session=", 0) == 0) j.sessions.push_back(Wide(line.substr(8)));
            else if (line.rfind("order=", 0) == 0) { j.order.clear(); for (const auto& id : Split(line.substr(6), ',')) { const std::wstring w = Wide(id); if (ValidId(w)) j.order.push_back(w); } }   // absent in older journals: index fallback
        } catch (...) { return false; }
    }
    std::istringstream rin(records); bool bad = false; const Doc d = Read(rin, &bad);
    if (bad || d.items.size() != 1) return false;
    j.profile = d.items.front();
    return !j.state.empty();
}
inline ULONGLONG WallNow() { FILETIME f; GetSystemTimeAsFileTime(&f); return ((static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10000; }   // ms
inline bool MoveEntry(const std::wstring& from, const std::wstring& to) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) return true;   // never REPLACE_EXISTING: a collision is refused
        const DWORD e = GetLastError(); if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED && e != ERROR_LOCK_VIOLATION) return false;
        Sleep(10);
    }
    return false;
}
// Moves back every entry that is at its destination and whose source is free. True when all are back.
inline bool RollBack(const std::vector<Move>& moves) {
    bool ok = true;
    for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
        const bool atDest = Exists(it->to), atSource = Exists(it->from);
        if (atDest && !atSource) ok = MoveEntry(it->to, it->from) && ok;
        else if (atDest && atSource) ok = false;   // both exist: never overwrite, keep both and report
    }
    return ok;
}
inline bool HoldAlive(const std::wstring& name) { if (name.empty()) return false; HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str()); if (!h) return false; CloseHandle(h); return true; }
// Position for `id` in `ids` (current list): right after its nearest predecessor in `order` (the list
// recorded at delete time, ghosts included) that is present now; first if none is; `index` only
// when `order` does not know `id` (journal written before `order` existed).
inline int PlaceFor(const std::vector<std::wstring>& ids, const std::wstring& id, const std::vector<std::wstring>& order, int index) {
    const auto k = std::find(order.begin(), order.end(), id);
    if (k == order.end()) return std::clamp(index, 0, static_cast<int>(ids.size()));
    for (auto it = std::make_reverse_iterator(k); it != order.rend(); ++it) {
        const auto at = std::find(ids.begin(), ids.end(), *it);
        if (at != ids.end()) return static_cast<int>(at - ids.begin()) + 1;
    }
    return 0;
}
inline void Reinsert(Registry& reg, const Profile& p, int index, const std::vector<std::wstring>& order = {}) {
    if (reg.Find(p.id)) return;
    std::vector<std::wstring> ids; for (const auto& q : reg.items) ids.push_back(q.id);
    reg.items.insert(reg.items.begin() + PlaceFor(ids, p.id, order, index), p);
}
// The list's ids now, plus the profiles still set aside by other pending deletes at their recorded
// places (oldest delete first). Caller holds the root lock.
inline std::vector<std::wstring> OrderWithGhosts(const std::wstring& root, const Registry& reg) {
    std::vector<std::wstring> ids; for (const auto& q : reg.items) ids.push_back(q.id);
    std::vector<std::pair<std::pair<ULONGLONG, std::wstring>, Journal>> pending; std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(TrashRoot(root), ec)) {
        if (!e.is_directory(ec)) continue;
        const std::wstring tomb = e.path().wstring(); Journal j;
        if (SafeTrashPath(root, tomb) && ReadJournal(root, tomb, j) && j.state == "aside") pending.push_back({{j.deadlineWall, e.path().filename().wstring()}, j});
    }
    std::sort(pending.begin(), pending.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& g : pending) if (std::find(ids.begin(), ids.end(), g.second.profile.id) == ids.end()) ids.insert(ids.begin() + PlaceFor(ids, g.second.profile.id, g.second.order, g.second.index), g.second.profile.id);
    return ids;
}
} // namespace Detail

// Deletes `id` (Paper P04): refuses the last profile, a read-only registry and a profile whose
// windows are still open (close them first, with WindowSessions::Release(keep=true) so their
// tabs survive for « Rouvrir ses fenêtres »). The profile's data is MOVED (never copied, never
// erased) into <root>\profiles\.trash\<id>-<tick>\data, recorded in a journal first. On any
// failure everything moved goes back and `error` says why (« in-use », « open-windows »,
// « last », « busy », …): nothing has been erased. On success `out` holds what Undo needs and a
// mutex that marks the undo window; call Finalize after UndoMilliseconds (or never: PurgeTrash
// finalizes once this process is gone).
inline bool Delete(const std::wstring& root, Registry& reg, const std::wstring& id, Tombstone& out, std::wstring& error) {
    error.clear();
    const Profile* found = reg.Find(id);
    if (reg.readOnly) { error = L"read-only"; return false; }
    if (!found) { error = L"unknown"; return false; }
    if (reg.items.size() <= 1) { error = L"last"; return false; }
    const Profile profile = *found;
    int index = 0; for (std::size_t i = 0; i < reg.items.size(); ++i) if (reg.items[i].id == id) index = static_cast<int>(i);
    const auto paths = ResolvePaths(root, id);
    ProfileStore::Lock rootLock(root, 10000); if (!rootLock.Held()) { error = L"busy"; return false; }
    ProfileStore::Lock dataLock(paths.dataDir, 10000); if (!dataLock.Held()) { error = L"busy"; return false; }
    if (!OpenSessionIds(paths.dataDir).empty()) { error = L"open-windows"; return false; }
    std::error_code ec; std::filesystem::create_directories(Detail::TrashRoot(root), ec);
    const std::wstring tomb = Detail::TrashRoot(root) + L"\\" + id + L"-" + std::to_wstring(GetTickCount64());
    if (!Detail::SafeTrashPath(root, tomb) || !CreateDirectoryW(tomb.c_str(), nullptr) || !CreateDirectoryW((tomb + L"\\data").c_str(), nullptr)) { error = L"trash"; Detail::DeleteTree(tomb); return false; }
    Detail::Journal j; j.state = "moving"; j.profile = profile; j.index = index; j.deadlineWall = Detail::WallNow() + UndoMilliseconds; j.order = Detail::OrderWithGhosts(root, reg);
    j.holdName = L"Local\\browser-profile-undo-" + ProfileStore::MutexName(root).substr(ProfileStore::MutexName(root).rfind(L'-') + 1) + L"-" + std::filesystem::path(tomb).filename().wstring();
    if (id == DefaultId) {
        WIN32_FIND_DATAW fd; HANDLE f = FindFirstFileW((root + L"\\*").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
                for (const auto& base : DefaultEntries()) if (Detail::Lower(n) == base || Detail::StartsWithI(n, base + L".")) { j.moves.push_back({root + L"\\" + n, tomb + L"\\data\\" + n}); break; }
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    } else if (Detail::Exists(paths.dataDir)) j.moves.push_back({paths.dataDir, tomb + L"\\data\\" + id});
    for (const auto& e : WindowSessions::List(paths.dataDir)) j.sessions.push_back(e.id);
    if (!Detail::WriteJournal(root, tomb, j)) { error = L"journal"; Detail::DeleteTree(tomb); return false; }
    // Owner marker created BEFORE the moves: a concurrent PurgeTrash never touches this tomb.
    HANDLE hold = CreateMutexW(nullptr, TRUE, j.holdName.c_str());
    for (std::size_t i = 0; i < j.moves.size(); ++i) {
        if (!Detail::MoveEntry(j.moves[i].from, j.moves[i].to)) {
            const std::vector<Detail::Move> done(j.moves.begin(), j.moves.begin() + static_cast<std::ptrdiff_t>(i));
            if (Detail::RollBack(done)) Detail::DeleteTree(tomb);   // journal kept if something could not go back
            if (hold) CloseHandle(hold);
            error = L"in-use"; return false;
        }
    }
    j.state = "aside";
    Registry next = reg; next.items.erase(std::remove_if(next.items.begin(), next.items.end(), [&](const Profile& p) { return p.id == id; }), next.items.end());
    if (next.last == id) next.last = next.items.empty() ? std::wstring() : next.items.front().id;
    if (!Detail::WriteJournal(root, tomb, j) || !Save(root, next)) {
        if (Detail::RollBack(j.moves)) Detail::DeleteTree(tomb);
        if (hold) CloseHandle(hold);
        error = L"busy"; return false;
    }
    reg = next;
    out = Tombstone{}; out.profile = profile; out.trashDir = tomb; out.closedSessions = j.sessions; out.index = index; out.deadline = GetTickCount64() + UndoMilliseconds; out.hold = hold;
    return true;
}
// Puts the profile back exactly (files byte for byte, its sessions as orphans ready for
// ReopenSessions) at its former place in the list. Refuses (keeps the tomb, overwrites nothing)
// if a destination exists again.
inline bool Undo(const std::wstring& root, Registry& reg, const Tombstone& t) {
    if (reg.readOnly || !Detail::SafeTrashPath(root, t.trashDir)) return false;
    ProfileStore::Lock rootLock(root, 10000); if (!rootLock.Held()) return false;
    Detail::Journal j; if (!Detail::ReadJournal(root, t.trashDir, j) || j.state != "aside") return false;
    for (const auto& m : j.moves) if (Detail::Exists(m.from)) return false;   // collision: refused
    j.state = "restoring"; if (!Detail::WriteJournal(root, t.trashDir, j)) return false;
    if (!Detail::RollBack(j.moves)) {   // partial: put back what came back, stay set aside
        for (const auto& m : j.moves) if (Detail::Exists(m.from) && !Detail::Exists(m.to)) Detail::MoveEntry(m.from, m.to);
        j.state = "aside"; Detail::WriteJournal(root, t.trashDir, j); return false;
    }
    Registry next = reg; Detail::Reinsert(next, j.profile, j.index, j.order);
    if (!Save(root, next)) {
        for (const auto& m : j.moves) Detail::MoveEntry(m.from, m.to);
        j.state = "aside"; Detail::WriteJournal(root, t.trashDir, j); return false;
    }
    reg = next;
    Detail::DeleteTree(t.trashDir);
    if (t.hold) CloseHandle(t.hold);
    return true;
}
// Ends the undo window: erases the tomb for good (only inside <root>\profiles\.trash).
inline bool Finalize(const std::wstring& root, Tombstone& t) {
    if (!Detail::SafeTrashPath(root, t.trashDir)) return false;
    ProfileStore::Lock rootLock(root, 10000); if (!rootLock.Held()) return false;
    Detail::Journal j; if (Detail::ReadJournal(root, t.trashDir, j) && j.state != "aside") return false;
    const bool ok = Detail::DeleteTree(t.trashDir);
    if (t.hold) { CloseHandle(t.hold); t.hold = nullptr; }
    return ok;
}
// Run at start (Load does it) and any time: rolls back interrupted deletes (« moving »),
// completes interrupted undos (« restoring »), keeping the profile's metadata in the registry,
// and erases set-aside profiles whose undo window is over (owner gone or deadline passed).
inline void PurgeTrash(const std::wstring& root) {
    const std::wstring trash = Detail::TrashRoot(root);
    if (!Detail::Exists(trash)) return;
    ProfileStore::Lock rootLock(root, 10000); if (!rootLock.Held()) return;
    std::vector<std::wstring> tombs; std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(trash, ec)) if (e.is_directory(ec)) tombs.push_back(e.path().wstring());
    for (const auto& tomb : tombs) {
        if (!Detail::SafeTrashPath(root, tomb)) continue;
        Detail::Journal j;
        if (!Detail::ReadJournal(root, tomb, j)) {
            // No readable journal: an aborted tomb with nothing moved is erased; anything else is kept.
            bool empty = true; for (const auto& e : std::filesystem::recursive_directory_iterator(tomb, ec)) { if (e.is_regular_file(ec)) { empty = false; break; } }
            if (empty) Detail::DeleteTree(tomb);
            continue;
        }
        if (j.state == "moving" || j.state == "restoring") {
            if (!Detail::RollBack(j.moves)) continue;   // could not complete: keep everything for the next try
            Detail::Doc base, mine; std::string bytes;
            if (ProfileStore::ReadAll(Detail::RegistryFile(root), bytes) && !bytes.empty() && ProfileStore::FirstLine(bytes) == Header) {
                std::istringstream in(bytes); base = Detail::Read(in, nullptr); mine = base;
                Registry r = Detail::ToRegistry(mine, base); Detail::Reinsert(r, j.profile, j.index, j.order); mine.items = r.items;
                if (!(mine == base) && Detail::Commit(root, base, mine) != ProfileStore::Result::Written) continue;
            }
            Detail::DeleteTree(tomb);
            continue;
        }
        // Still in its undo window as long as its owner holds the mutex, WHATEVER the deadline: the
        // notice pauses its countdown on hover/focus, so the owner may legitimately go past it. The
        // mutex dies with its process (crash included), so nothing is held forever.
        if (j.state != "aside" || Detail::HoldAlive(j.holdName)) continue;
        // Crash between « aside » and the registry write: the profile is still listed, so the delete
        // never completed. Put it back instead of erasing data the registry still points to.
        std::string bytes; bool listed = false;
        if (ProfileStore::ReadAll(Detail::RegistryFile(root), bytes) && ProfileStore::FirstLine(bytes) == Header) { std::istringstream in(bytes); for (const auto& p : Detail::Read(in, nullptr).items) listed = listed || p.id == j.profile.id; }
        else if (!bytes.empty()) continue;   // unknown registry: touch nothing
        if (listed) { if (Detail::RollBack(j.moves)) Detail::DeleteTree(tomb); continue; }
        Detail::DeleteTree(tomb);
    }
}

} // namespace Profiles
