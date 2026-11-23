#pragma once
// Shared helpers of the change-snapshot tests (tests/snapshots/). Everything happens in a temporary folder
// under %TEMP%: no real project, no real configuration, no git.
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../src/change-snapshots.h"

namespace SnapTest {

inline int g_checks = 0;
inline int g_failures = 0;
inline const char* g_section = "";

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        ++SnapTest::g_checks;                                                                 \
        if (!(cond)) {                                                                        \
            ++SnapTest::g_failures;                                                           \
            std::printf("  FAIL [%s] %s:%d: %s\n", SnapTest::g_section, __FILE__, __LINE__, #cond); \
        }                                                                                     \
    } while (0)

#define CHECK_EQ(a, b)                                                                        \
    do {                                                                                      \
        ++SnapTest::g_checks;                                                                 \
        const auto va_ = (a);                                                                 \
        const auto vb_ = (b);                                                                 \
        if (!(va_ == vb_)) {                                                                  \
            ++SnapTest::g_failures;                                                           \
            std::printf("  FAIL [%s] %s:%d: %s == %s\n    left : %s\n    right: %s\n", SnapTest::g_section, __FILE__, __LINE__, \
                        #a, #b, SnapTest::Show(va_).c_str(), SnapTest::Show(vb_).c_str());     \
        }                                                                                     \
    } while (0)

inline std::string Show(const std::string& s) { return s.size() > 300 ? s.substr(0, 300) + "..." : s; }
inline std::string Show(const char* s) { return s; }
inline std::string Show(bool b) { return b ? "true" : "false"; }
inline std::string Show(long long v) { return std::to_string(v); }
inline std::string Show(unsigned long long v) { return std::to_string(v); }
inline std::string Show(int v) { return std::to_string(v); }
inline std::string Show(unsigned v) { return std::to_string(v); }
inline std::string Show(long v) { return std::to_string(v); }
inline std::string Show(unsigned long v) { return std::to_string(v); }
inline std::string Show(ChangeSnapshots::Status s) { return ChangeSnapshots::StatusName(s); }
inline std::string Show(ChangeSnapshots::ChangeKind k) { return ChangeSnapshots::ChangeKindName(k); }
inline std::string Show(ChangeSnapshots::NotKept k) { return ChangeSnapshots::NotKeptName(k); }
inline std::string Show(ChangeSnapshots::ConflictReason r) { return ChangeSnapshots::ConflictReasonName(r); }

inline std::wstring Widen(std::string_view s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
inline std::string Narrow(std::wstring_view w) {
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
inline std::wstring Long(const std::wstring& path) { return path.rfind(L"\\\\?\\", 0) == 0 ? path : L"\\\\?\\" + path; }
inline std::wstring RelW(std::string_view rel) {
    std::wstring w = Widen(rel);
    for (auto& c : w)
        if (c == L'/') c = L'\\';
    return w;
}

inline void MakeDirs(const std::wstring& dir) {
    if (dir.size() <= 3) return;
    if (GetFileAttributesW(Long(dir).c_str()) != INVALID_FILE_ATTRIBUTES) return;
    const auto slash = dir.find_last_of(L'\\');
    if (slash != std::wstring::npos) MakeDirs(dir.substr(0, slash));
    CreateDirectoryW(Long(dir).c_str(), nullptr);
}

inline bool WriteFileBytes(const std::wstring& path, std::string_view data) {
    const auto slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) MakeDirs(path.substr(0, slash));
    const HANDLE h = CreateFileW(Long(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    std::size_t done = 0;
    bool ok = true;
    while (ok && done < data.size()) {
        ok = WriteFile(h, data.data() + done, DWORD(std::min<std::size_t>(data.size() - done, 1u << 24)), &wrote, nullptr) != 0;
        done += wrote;
    }
    CloseHandle(h);
    return ok;
}

inline std::optional<std::string> ReadFileBytes(const std::wstring& path) {
    const HANDLE h = CreateFileW(Long(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);
    std::string out(std::size_t(size.QuadPart), '\0');
    std::size_t done = 0;
    DWORD got = 0;
    while (done < out.size() && ReadFile(h, out.data() + done, DWORD(std::min<std::size_t>(out.size() - done, 1u << 24)), &got, nullptr) && got)
        done += got;
    CloseHandle(h);
    out.resize(done);
    return out;
}

inline bool PathExists(const std::wstring& path) { return GetFileAttributesW(Long(path).c_str()) != INVALID_FILE_ATTRIBUTES; }
inline bool IsDir(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(Long(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Removes a tree without ever following a link.
inline void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW(Long(dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring full = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) RemoveDirectoryW(Long(full).c_str());
                else RemoveTree(full);
            } else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(Long(full).c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(Long(full).c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(Long(dir).c_str());
}

// Every file under `dir` (relative path with '/', content). `skip`: a top-level folder name left out.
inline void CollectTree(const std::wstring& dir, const std::string& rel, std::map<std::string, std::string>& out,
                        const wchar_t* skip = nullptr) {
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW(Long(dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (skip && rel.empty() && name == skip) continue;
        const std::wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CollectTree(full, rel + Narrow(name) + "/", out);
        } else {
            out[rel + Narrow(name)] = ReadFileBytes(full).value_or("<unreadable>");
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
inline std::map<std::string, std::string> Tree(const std::wstring& dir, const wchar_t* skip = nullptr) {
    std::map<std::string, std::string> out;
    CollectTree(dir, "", out, skip);
    return out;
}
inline std::size_t CountFiles(const std::wstring& dir, const std::wstring& suffix) {
    std::size_t n = 0;
    for (const auto& item : Tree(dir)) {
        const std::wstring name = Widen(item.first);
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) ++n;
    }
    return n;
}

// Moves a file's date into the past, so the "too recent to trust" rule does not force a re-read.
inline void AgeFile(const std::wstring& path, int secondsAgo = 3600) {
    const HANDLE h = CreateFileW(Long(path).c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t;
    t.LowPart = now.dwLowDateTime;
    t.HighPart = now.dwHighDateTime;
    t.QuadPart -= std::uint64_t(secondsAgo) * 10000000ull;
    FILETIME past;
    past.dwLowDateTime = t.LowPart;
    past.dwHighDateTime = t.HighPart;
    SetFileTime(h, nullptr, nullptr, &past);
    CloseHandle(h);
}
inline void AgeTree(const std::wstring& dir, int secondsAgo = 3600) {
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW(Long(dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) AgeTree(full, secondsAgo);
        else AgeFile(full, secondsAgo);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Deterministic pseudo-random bytes / text.
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed * 2654435761u + 0x9E3779B97F4A7C15ull) {}
    std::uint32_t Next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return std::uint32_t(state >> 33);
    }
    std::string Bytes(std::size_t n) {
        std::string out(n, '\0');
        for (auto& c : out) c = char(Next() & 0xFF);
        return out;
    }
    std::string Text(std::size_t lines, const char* eol = "\n") {
        std::string out;
        for (std::size_t i = 0; i < lines; ++i) {
            out += "line " + std::to_string(i) + " value " + std::to_string(Next() % 100000);
            out += eol;
        }
        return out;
    }
};

// One temporary project + one temporary store.
struct Sandbox {
    std::wstring root, project, store;

    explicit Sandbox(const char* name) {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        std::wstring base = temp;
        while (!base.empty() && base.back() == L'\\') base.pop_back();
        root = base + L"\\browser-snapshots-test-" + std::to_wstring(GetCurrentProcessId()) + L"\\" + Widen(name);
        RemoveTree(root);
        project = root + L"\\project";
        store = root + L"\\data\\chat-snapshots";
        MakeDirs(project);
    }
    ~Sandbox() {
        RemoveTree(root);
        RemoveDirectoryW(Long(root.substr(0, root.find_last_of(L'\\'))).c_str());  // the per-process folder, once empty
    }

    std::wstring Path(std::string_view rel) const { return project + L"\\" + RelW(rel); }
    void Put(std::string_view rel, std::string_view data, bool aged = true) const {
        WriteFileBytes(Path(rel), data);
        if (aged) AgeFile(Path(rel));
    }
    std::optional<std::string> Get(std::string_view rel) const { return ReadFileBytes(Path(rel)); }
    bool Has(std::string_view rel) const { return PathExists(Path(rel)); }
    void Remove(std::string_view rel) const { DeleteFileW(Long(Path(rel)).c_str()); }
    std::map<std::string, std::string> Files() const { return Tree(project); }
};

inline const ChangeSnapshots::FileChange* FindChange(const ChangeSnapshots::TurnSummary& summary, std::string_view path) {
    for (const auto& c : summary.changes)
        if (c.path == path) return &c;
    return nullptr;
}
inline bool HasConflict(const ChangeSnapshots::Result& r, std::string_view path, ChangeSnapshots::ConflictReason reason) {
    for (const auto& c : r.conflicts)
        if (c.path == path && c.reason == reason) return true;
    return false;
}
inline bool Contains(const std::vector<std::string>& list, std::string_view item) {
    for (const auto& s : list)
        if (s == item) return true;
    return false;
}

// One whole turn: BeginTurn, the agent's edits, EndTurn.
inline ChangeSnapshots::TurnSummary RunTurn(ChangeSnapshots::Store& store, const Sandbox& box, const std::function<void()>& edits) {
    ChangeSnapshots::TurnSummary summary;
    std::string turnId;
    const ChangeSnapshots::Result begun = store.BeginTurn(box.project, &turnId);
    CHECK_EQ(begun.status, ChangeSnapshots::Status::Ok);
    if (!begun) std::printf("    BeginTurn: %s\n", begun.detail.c_str());
    edits();
    const ChangeSnapshots::Result ended = store.EndTurn(&summary);
    CHECK_EQ(ended.status, ChangeSnapshots::Status::Ok);
    if (!ended) std::printf("    EndTurn: %s\n", ended.detail.c_str());
    CHECK_EQ(summary.turnId, turnId);
    return summary;
}

}  // namespace SnapTest
