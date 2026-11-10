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
#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

namespace ProfileStore {

// Stable per profile: FNV-1a of the lower-cased absolute data directory.
inline std::wstring MutexName(const std::wstring& dataDir) {
    std::wstring key = dataDir;
    wchar_t full[MAX_PATH * 2]{};
    if (GetFullPathNameW(dataDir.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr)) key = full;
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

// Complete temp file (unique to this process), flushed, then renamed over the target.
inline bool WriteAtomic(const std::wstring& path, const std::string& data) {
    const std::wstring temp = path + L".tmp" + std::to_wstring(GetCurrentProcessId());
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(f);
    CloseHandle(f);
    // A reader outside this store (indexer, antivirus, an older build) may hold the target open
    // without delete sharing for a moment: retry the rename briefly instead of failing the commit.
    bool moved = false;
    for (int attempt = 0; ok && attempt < 100 && !(moved = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)); ++attempt) {
        const DWORD e = GetLastError(); if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED && e != ERROR_LOCK_VIOLATION) break;
        Sleep(10);
    }
    if (!moved) { DeleteFileW(temp.c_str()); return false; }
    return true;
}

enum class Result { Written, Unchanged, Busy, Failed };

// One transaction on `path` for a model with `static M Read(std::istream&, bool*)`, `Write(std::ostream&)`
// and `operator==`. `merge(base, mine, theirs)` returns the new state. On success `mine` and `base`
// both become that state (the window now also shows the other windows' changes).
template <class M>
Result Commit(const std::wstring& dataDir, const std::wstring& path, M& base, M& mine,
              const std::function<M(const M& base, const M& mine, const M& theirs)>& merge, DWORD timeoutMs = 5000) {
    Lock lock(dataDir, timeoutMs);
    if (!lock.Held()) return Result::Busy;
    std::string bytes;
    if (!ReadAll(path, bytes)) return Result::Failed;
    std::istringstream in(bytes); bool damaged = false;
    const M theirs = M::Read(in, &damaged);
    M merged = merge(base, mine, theirs);
    if (merged == theirs && !damaged) { base = merged; mine = std::move(merged); return Result::Unchanged; }
    std::ostringstream out; merged.Write(out);
    if (!WriteAtomic(path, out.str())) return Result::Failed;
    base = merged; mine = std::move(merged);
    return Result::Written;
}

} // namespace ProfileStore
