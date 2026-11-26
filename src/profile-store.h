#pragma once
// Interprocess profile store (decision 2, « A renforcée »): one process per normal window, all of
// them sharing one profile directory. Every shared file is changed only inside a transaction:
//   1. take the profile's named mutex (Local\browser-profile-<hash of the data directory>);
//   2. read the file as it is NOW (another window may have written it);
//   3. merge this process's own changes (base → mine) onto it (the model's Merge);
//   4. write it atomically (unique temp file, flushed, MoveFileEx REPLACE_EXISTING|WRITE_THROUGH);
//   5. release the mutex; the merged result becomes both the in-memory state and the new base.
// A process that dies while holding the mutex leaves it abandoned: the next owner gets
// WAIT_ABANDONED, which still grants ownership; the file itself is never half-written because
// only a complete temp file is ever renamed over it. Re-reading alone (without the mutex) gives
// no atomicity: two windows could still interleave read and write. Pure Win32 + C++17.
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <vector>
#include <sstream>
#include <string>

namespace ProfileStore {

// Stable per profile: FNV-1a of the lower-cased absolute data directory.
inline std::wstring MutexName(const std::wstring& dataDir) {
    std::wstring key = dataDir;
    wchar_t full[MAX_PATH * 2]{};
    if (GetFullPathNameW(dataDir.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr)) key = full;
    // Review S4: one profile reached through an 8.3 name, a junction or subst gets ONE lock.
    HANDLE dir = CreateFileW(key.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (dir != INVALID_HANDLE_VALUE) {
        wchar_t final[MAX_PATH * 2]{};
        const DWORD n = GetFinalPathNameByHandleW(dir, final, static_cast<DWORD>(std::size(final)), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
        if (n > 0 && n < std::size(final)) key = final;
        CloseHandle(dir);
    }
    while (!key.empty() && (key.back() == L'\\' || key.back() == L'/')) key.pop_back();
    std::uint64_t h = 1469598103934665603ull;
    for (wchar_t c : key) { c = static_cast<wchar_t>(towlower(c == L'/' ? L'\\' : c)); h ^= static_cast<std::uint64_t>(c); h *= 1099511628211ull; }
    wchar_t text[17]; swprintf(text, 17, L"%016llx", static_cast<unsigned long long>(h));
    return std::wstring(L"Local\\browser-profile-") + text;
}

// RAII owner of the profile mutex. `held` is false only on a timeout or a failure: the caller
// then keeps its changes in memory and retries later, it never writes without the lock.
class Lock {
public:
    explicit Lock(const std::wstring& dataDir, DWORD timeoutMs = 5000) {
        handle_ = CreateMutexW(nullptr, FALSE, MutexName(dataDir).c_str());
        if (!handle_) return;
        const DWORD w = WaitForSingleObject(handle_, timeoutMs);
        held_ = w == WAIT_OBJECT_0 || w == WAIT_ABANDONED;
        abandoned_ = w == WAIT_ABANDONED;
    }
    ~Lock() { if (held_) ReleaseMutex(handle_); if (handle_) CloseHandle(handle_); }
    Lock(const Lock&) = delete; Lock& operator=(const Lock&) = delete;
    bool Held() const { return held_; }
    bool Abandoned() const { return abandoned_; }   // the previous owner died holding it
private:
    HANDLE handle_ = nullptr; bool held_ = false, abandoned_ = false;
};

// Whole file as bytes; a missing file is an empty string (and true). Opened with full sharing
// (read, write, delete) so a reader never blocks a writer's rename.
inline bool ReadAll(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { const DWORD e = GetLastError(); return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND; }
    LARGE_INTEGER size{}; bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (1ll << 30);
    if (ok) { out.resize(static_cast<std::size_t>(size.QuadPart)); DWORD got = 0; ok = out.empty() || (ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size()); }
    CloseHandle(f);
    if (!ok) out.clear();
    return ok;
}

// Identity of a file as it is on disk now (PERF-01/08): volume, file id, size and write time. Every
// WriteAtomic renames a NEW file over the target, so any write by any window changes the file id. A
// stamp equal to one taken earlier means the file was not replaced or modified in between. `known`
// is false when the file could not be inspected: such a stamp never equals another.
struct Stamp {
    bool known = false, exists = false; DWORD volume = 0, idHigh = 0, idLow = 0, sizeHigh = 0, sizeLow = 0; FILETIME written{};
    bool operator==(const Stamp& o) const {
        return known && o.known && exists == o.exists && volume == o.volume && idHigh == o.idHigh && idLow == o.idLow && sizeHigh == o.sizeHigh && sizeLow == o.sizeLow
            && written.dwLowDateTime == o.written.dwLowDateTime && written.dwHighDateTime == o.written.dwHighDateTime;
    }
    bool operator!=(const Stamp& o) const { return !(*this == o); }
};
inline Stamp StampOf(const std::wstring& path) {
    Stamp s;
    HANDLE f = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { const DWORD e = GetLastError(); s.known = e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND; return s; }
    BY_HANDLE_FILE_INFORMATION i{};
    if (GetFileInformationByHandle(f, &i)) { s.known = s.exists = true; s.volume = i.dwVolumeSerialNumber; s.idHigh = i.nFileIndexHigh; s.idLow = i.nFileIndexLow; s.sizeHigh = i.nFileSizeHigh; s.sizeLow = i.nFileSizeLow; s.written = i.ftLastWriteTime; }
    CloseHandle(f);
    return s;
}

// Complete temp file (unique to this process), flushed, then renamed over the target.
// `durable` = false (PERF-08): for a hint file rebuilt at the next start (the window sidecar), the
// same atomic rename but without FlushFileBuffers or a write-through rename.
inline bool WriteAtomic(const std::wstring& path, const std::string& data, bool durable = true) {
    const std::wstring temp = path + L".tmp" + std::to_wstring(GetCurrentProcessId());
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && (!durable || FlushFileBuffers(f));
    CloseHandle(f);
    // A reader outside this store (indexer, antivirus, an older build) may hold the target open
    // without delete sharing for a moment: retry the rename briefly instead of failing the commit.
    bool moved = false;
    for (int attempt = 0; ok && attempt < 100 && !(moved = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | (durable ? MOVEFILE_WRITE_THROUGH : 0))); ++attempt) {
        const DWORD e = GetLastError(); if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED && e != ERROR_LOCK_VIOLATION) break;
        Sleep(10);
    }
    if (!moved) { DeleteFileW(temp.c_str()); return false; }
    return true;
}

// Decision S3 (fail closed). Outcomes are distinct: Written / Unchanged; Busy (lock timeout);
// Failed (read or write error, or the exact backup of a damaged file could not be made);
// UnknownFormat (header of the same family but another version, or not a known header at all
// while the file is not empty). In Busy, Failed and UnknownFormat NOTHING is written, and `base`
// and `mine` are untouched: the caller keeps its changes and says so; a retry is explicit.
enum class Result { Written, Unchanged, Busy, Failed, UnknownFormat };
// Test hook only: fixes the « .damaged-<stamp> » suffix so a test can make the backup fail.
inline std::wstring& DamagedStampForTest() { static std::wstring s; return s; }
// Record lines after the header (non-empty). A lenient reader that returns fewer records than this
// silently dropped some (malformed, duplicate, over a limit, cut mid-line): the caller treats the file
// as damaged and never rewrites it from what it could read (review S3 / A5).
inline std::size_t RecordLines(const std::string& bytes) {
    std::size_t n = 0, start = bytes.find('\n'); if (start == std::string::npos) return 0; ++start;
    while (start < bytes.size()) { std::size_t e = bytes.find('\n', start); std::size_t len = (e == std::string::npos ? bytes.size() : e) - start; if (len > 0 && !(len == 1 && bytes[start] == '\r')) ++n; if (e == std::string::npos) { ++n; break; } start = e + 1; }
    return n;   // a last line without its newline counts twice: a cut file never matches
}
inline std::string FirstLine(const std::string& bytes) { std::string l = bytes.substr(0, bytes.find('\n')); if (!l.empty() && l.back() == '\r') l.pop_back(); return l; }

// `known`: the header lines this build reads (e.g. Bookmarks::Headers()). Empty = no check.
// A known format that is damaged (bad line, truncation) is copied aside BYTE FOR BYTE, the copy is
// read back and compared BEFORE anything is written; if that fails, nothing is written. What could
// be read is then also taken as the base, so nothing this window holds is mistaken for « deleted
// elsewhere ». Unreadable is never treated as « deleted ».
template <class M>
Result CommitWith(const std::wstring& dataDir, const std::wstring& path, M& base, M& mine,
                  const std::function<M(std::istream&, bool* damaged)>& read, const std::function<void(std::ostream&, const M&)>& write,
                  const std::function<M(const M& base, const M& mine, const M& theirs)>& merge, DWORD timeoutMs = 5000,
                  const std::vector<std::string>& known = {}, bool writeOverDamaged = true,
                  const std::function<bool(const M&)>& acceptable = {}) {
    Lock lock(dataDir, timeoutMs);
    if (!lock.Held()) return Result::Busy;
    std::string bytes;
    if (!ReadAll(path, bytes)) return Result::Failed;
    // Review V2-M1: a file that EXISTS with zero bytes is not « absent »: with a known format it is damage.
    const bool exists = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (exists && bytes.empty() && !known.empty() && !writeOverDamaged) return Result::Failed;
    if (!bytes.empty() && !known.empty()) {
        const std::string head = FirstLine(bytes);
        if (std::find(known.begin(), known.end(), head) == known.end()) return Result::UnknownFormat;   // future version or not ours: never overwritten
    }
    std::istringstream in(bytes); bool damaged = false;
    const M theirs = read(in, &damaged);
    if (damaged && !bytes.empty()) {
        std::wstring stamp = DamagedStampForTest();
        if (stamp.empty()) { SYSTEMTIME t; GetLocalTime(&t); wchar_t b[32]; swprintf(b, 32, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); stamp = b; }
        const std::wstring aside = path + L".damaged-" + stamp; std::string check;
        // One backup per distinct damaged content (a retry does not pile up copies, review low).
        std::string previous; const std::wstring last = path + L".damaged-last";
        const bool already = ReadAll(last, previous) && previous == bytes;
        if (!already && (!WriteAtomic(aside, bytes) || !ReadAll(aside, check) || check != bytes || !WriteAtomic(last, bytes))) return Result::Failed;   // no exact backup: no write
        if (!writeOverDamaged) return Result::Failed;   // e.g. the vault: backed up, never rewritten without an explicit recovery
    }
    M merged = damaged ? merge(theirs, mine, theirs) : merge(base, mine, theirs);
    if (acceptable && !acceptable(merged)) return Result::Failed;   // e.g. over a limit: nothing written, nothing dropped (review V2-M2)
    if (merged == theirs && !damaged) { base = merged; mine = std::move(merged); return Result::Unchanged; }
    std::ostringstream out; write(out, merged);
    if (!WriteAtomic(path, out.str())) return Result::Failed;
    base = merged; mine = std::move(merged);
    return Result::Written;
}
template <class M>
Result Commit(const std::wstring& dataDir, const std::wstring& path, M& base, M& mine,
              const std::function<M(const M& base, const M& mine, const M& theirs)>& merge, DWORD timeoutMs = 5000,
              const std::vector<std::string>& known = {}, bool writeOverDamaged = true, const std::function<bool(const M&)>& acceptable = {}) {
    return CommitWith<M>(dataDir, path, base, mine, [](std::istream& in, bool* d) { return M::Read(in, d); }, [](std::ostream& out, const M& m) { m.Write(out); }, merge, timeoutMs, known, writeOverDamaged, acceptable);
}
// Field-by-field merge of "key=value" line files (settings): every key this window changed since
// its base takes this window's value, every other key keeps the file's current value.
inline std::string MergeKeyValues(const std::string& base, const std::string& mine, const std::string& theirs) {
    auto parse = [](const std::string& text, std::vector<std::string>* order) {
        std::map<std::string, std::string> kv; std::istringstream in(text); std::string line; bool first = true;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (first) { first = false; continue; }
            const auto eq = line.find('='); if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq);
            if (order && !kv.count(k)) order->push_back(k);
            kv[k] = line.substr(eq + 1);
        }
        return kv;
    };
    std::vector<std::string> order; const auto b = parse(base, nullptr), m = parse(mine, &order); auto t = parse(theirs, nullptr);
    for (const auto& [k, v] : m) { const auto bi = b.find(k); if (bi == b.end() || bi->second != v) t[k] = v; }
    const std::string header = mine.substr(0, mine.find('\n'));
    std::string out = header + "\n";
    for (const auto& k : order) out += k + "=" + t[k] + "\n";
    for (const auto& [k, v] : t) if (!m.count(k)) out += k + "=" + v + "\n";
    return out;
}

} // namespace ProfileStore
