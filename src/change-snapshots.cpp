// Change snapshots (chat V1, contract C). Contract: src/change-snapshots.h and design/chat-snapshots.md.
// Win32 (kernel32, bcrypt) + C++17 only; no UI, no globals beyond constant tables. No git, no third-party code.
#include "change-snapshots.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "strict-json.h"

namespace ChangeSnapshots {
namespace {

using StrictJson::Value;
namespace SJ = StrictJson;

constexpr std::uint64_t RacyTicks = 2ull * 10000000ull;  // 2 s in FILETIME units: a date this recent is re-read
constexpr wchar_t TempSuffix[] = L".snaptmp";           // our temporary files inside the project
constexpr std::uint64_t MaxGitignoreBytes = 1024 * 1024;
constexpr int MaxDepth = 128;
constexpr int MaxDiffD = 2048;                 // exact diff up to this many changed lines
constexpr std::int64_t DiffBudget = 40000000;  // line comparisons before the diff gives up being minimal
constexpr const char* OpenTurnSchema = "browser-chat-snapshot-open-turn/1";
constexpr unsigned TurnLockWaitMs = 150;  // a lock file just released may still be going away

// ---------------------------------------------------------------- strings
bool ToUtf8(std::wstring_view w, std::string& out) {
    out.clear();
    if (w.empty()) return true;
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return false;  // a lone surrogate: the name cannot round-trip
    out.resize(std::size_t(n));
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
    return true;
}
std::string Narrow(std::wstring_view w) {
    std::string s;
    ToUtf8(w, s);
    return s;
}
std::wstring Widen(std::string_view s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
char Fold(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }
std::string LowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = Fold(c);
    return out;
}
bool EndsWith(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}
bool StartsWith(std::string_view s, std::string_view head) {
    return s.size() >= head.size() && s.compare(0, head.size(), head) == 0;
}
bool SameNoCaseW(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) == CSTR_EQUAL;
}

// ---------------------------------------------------------------- paths
// Every Win32 call gets the \\?\ form, so long paths work without a manifest.
std::wstring LongPath(const std::wstring& path) {
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    if (path.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + path.substr(2);
    return L"\\\\?\\" + path;
}
std::wstring RelToWide(std::string_view rel) {
    std::wstring w = Widen(rel);
    for (auto& c : w)
        if (c == L'/') c = L'\\';
    return w;
}
std::wstring ParentOf(const std::wstring& path) {
    const auto slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}
std::wstring LeafOf(const std::wstring& path) {
    const auto slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// A path read back from a record: relative, '/' separated, no "." / "..", nothing Windows forbids in a name,
// never inside a .git folder. Records are ours, but a damaged or edited one must not lead outside the project.
constexpr std::size_t MaxRelPathBytes = 32000;
bool IsSafeComponent(std::string_view part) {
    if (part.empty() || part == "." || part == "..") return false;
    if (part.back() == '.' || part.back() == ' ') return false;  // Win32 would silently drop them
    for (const char c : part) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    }
    return LowerAscii(part) != ".git";
}
bool IsSafeRelPath(std::string_view rel) {
    if (rel.empty() || rel.size() > MaxRelPathBytes || rel.front() == '/' || rel.back() == '/') return false;
    if (!SJ::ValidUtf8(rel)) return false;
    std::size_t start = 0;
    while (start <= rel.size()) {
        const std::size_t end = std::min(rel.find('/', start), rel.size());
        if (!IsSafeComponent(rel.substr(start, end - start))) return false;
        start = end + 1;
    }
    return true;
}

// ---------------------------------------------------------------- SHA-256, ids, time
BCRYPT_ALG_HANDLE ShaProvider() {
    static const BCRYPT_ALG_HANDLE handle = [] {
        BCRYPT_ALG_HANDLE h = nullptr;
        if (BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) h = nullptr;
        return h;
    }();
    return handle;
}
bool Sha256Hex(std::string_view data, std::string& hex) {
    hex.clear();
    BCRYPT_HASH_HANDLE h = nullptr;
    if (!ShaProvider() || BCryptCreateHash(ShaProvider(), &h, nullptr, 0, nullptr, 0, 0) != 0) return false;
    bool ok = true;
    std::size_t done = 0;
    while (ok && done < data.size()) {
        const ULONG chunk = ULONG(std::min<std::size_t>(data.size() - done, 1u << 30));
        ok = BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data() + done)), chunk, 0) == 0;
        done += chunk;
    }
    unsigned char digest[32] = {};
    ok = ok && BCryptFinishHash(h, digest, sizeof digest, 0) == 0;
    BCryptDestroyHash(h);
    if (!ok) return false;
    static const char* digits = "0123456789abcdef";
    hex.resize(64);
    for (int i = 0; i < 32; ++i) {
        hex[std::size_t(i) * 2] = digits[digest[i] >> 4];
        hex[std::size_t(i) * 2 + 1] = digits[digest[i] & 15];
    }
    return true;
}
bool IsHash(std::string_view s) {
    if (s.size() != 64) return false;
    for (const char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
// 26 lowercase base32 characters = 130 random bits.
std::string RandomSuffix() {
    unsigned char bytes[17] = {};
    if (BCryptGenRandom(nullptr, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        LARGE_INTEGER counter;  // never expected; ids only need to be unique within one project
        QueryPerformanceCounter(&counter);
        std::memcpy(bytes, &counter, sizeof counter);
        const DWORD pid = GetCurrentProcessId();
        std::memcpy(bytes + 8, &pid, sizeof pid);
    }
    static const char* alphabet = "abcdefghijklmnopqrstuvwxyz234567";
    std::string out;
    unsigned buffer = 0;
    int bits = 0;
    for (const unsigned char b : bytes) {
        buffer = (buffer << 8) | b;
        bits += 8;
        while (bits >= 5 && out.size() < 26) {
            out.push_back(alphabet[(buffer >> (bits - 5)) & 31]);
            bits -= 5;
        }
    }
    return out;
}
bool IsId(std::string_view id, std::string_view prefix) {
    if (id.size() != prefix.size() + 26 || !StartsWith(id, prefix)) return false;
    for (const char c : id.substr(prefix.size()))
        if (!((c >= 'a' && c <= 'z') || (c >= '2' && c <= '7'))) return false;
    return true;
}
std::uint64_t Ticks(const FILETIME& ft) { return (std::uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }
std::uint64_t NowTicks() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return Ticks(ft);
}
std::int64_t SystemSeconds() { return std::int64_t(NowTicks() / 10000000ull) - 11644473600ll; }
double ElapsedMs(const LARGE_INTEGER& from) {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return double(now.QuadPart - from.QuadPart) * 1000.0 / double(freq.QuadPart);
}

// ---------------------------------------------------------------- files
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE handle) : h(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { Close(); }
    bool Valid() const { return h != INVALID_HANDLE_VALUE; }
    void Close() {
        if (Valid()) CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
    void Take(Handle& other) {
        Close();
        h = other.h;
        other.h = INVALID_HANDLE_VALUE;
    }
};

bool EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    const DWORD attrs = GetFileAttributesW(LongPath(dir).c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const std::wstring parent = ParentOf(dir);
    if (!parent.empty() && parent.size() < dir.size() && parent.back() != L':' && !EnsureDir(parent)) return false;
    if (CreateDirectoryW(LongPath(dir).c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

struct Probe {
    enum Kind { Absent, File, Dir } kind = Absent;
    DWORD attrs = 0;
    std::uint64_t size = 0, mtime = 0;
};
// What stands at `full` right now. A name that differs only by case counts as absent: it is another file as far
// as the records go (an agent's case-only rename is "deleted + created").
Probe ProbePath(const std::wstring& full) {
    Probe p;
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileExW(LongPath(full).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return p;
    FindClose(h);
    if (LeafOf(full) != fd.cFileName) return p;
    p.attrs = fd.dwFileAttributes;
    p.kind = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? Probe::Dir : Probe::File;
    p.size = (std::uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    p.mtime = Ticks(fd.ftLastWriteTime);
    return p;
}

enum class ReadStatus { Ok, Missing, TooLarge, Denied };
// Reads a regular file whole. Never follows a link (the link itself is refused).
ReadStatus ReadWhole(const std::wstring& path, std::uint64_t maxBytes, std::string& out,
                     std::uint64_t* size = nullptr, std::uint64_t* mtime = nullptr) {
    out.clear();
    Handle f(CreateFileW(LongPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!f.Valid()) {
        const DWORD e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? ReadStatus::Missing : ReadStatus::Denied;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(f.h, &info)) return ReadStatus::Denied;
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return ReadStatus::Denied;
    const std::uint64_t total = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size) *size = total;
    if (mtime) *mtime = Ticks(info.ftLastWriteTime);
    if (total > maxBytes) return ReadStatus::TooLarge;
    out.resize(std::size_t(total));
    std::size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD want = DWORD(std::min<std::size_t>(out.size() - done, 1u << 24));
        if (!ReadFile(f.h, out.data() + done, want, &got, nullptr)) return ReadStatus::Denied;
        if (got == 0) break;  // shrank while we read
        done += got;
    }
    out.resize(done);
    return ReadStatus::Ok;
}

bool WriteNew(const std::wstring& path, std::string_view data, bool flush) {
    Handle f(CreateFileW(LongPath(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!f.Valid()) return false;
    std::size_t done = 0;
    bool ok = true;
    while (ok && done < data.size()) {
        DWORD wrote = 0;
        const DWORD want = DWORD(std::min<std::size_t>(data.size() - done, 1u << 24));
        ok = WriteFile(f.h, data.data() + done, want, &wrote, nullptr) && wrote > 0;
        done += wrote;
    }
    if (ok && flush) ok = FlushFileBuffers(f.h) != 0;
    f.Close();
    if (!ok) DeleteFileW(LongPath(path).c_str());
    return ok;
}

// MoveFileExW with a few retries: an antivirus or an indexer may hold the file for a moment.
bool MoveReplace(const std::wstring& from, const std::wstring& to, bool replace, bool through) {
    const DWORD flags = (replace ? MOVEFILE_REPLACE_EXISTING : 0) | (through ? MOVEFILE_WRITE_THROUGH : 0);
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(LongPath(from).c_str(), LongPath(to).c_str(), flags)) return true;
        const DWORD e = GetLastError();
        if (attempt >= 5 || (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED && e != ERROR_LOCK_VIOLATION)) {
            SetLastError(e);
            return false;
        }
        Sleep(20);
    }
}

// Store files: temporary file + rename, so a reader never sees a half-written record.
bool WriteAtomic(const std::wstring& path, std::string_view data) {
    if (!EnsureDir(ParentOf(path))) return false;
    const std::wstring temp = path + L".tmp-" + Widen(RandomSuffix().substr(0, 8));
    if (!WriteNew(temp, data, true)) return false;
    if (MoveReplace(temp, path, true, true)) return true;
    DeleteFileW(LongPath(temp).c_str());
    return false;
}

bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(LongPath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Deletes a folder's content (never through a link). `keep`: one file name left in place at the top level.
void DeleteTree(const std::wstring& dir, const wchar_t* keep, bool removeSelf) {
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileExW(LongPath(dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            if (keep && name == keep) continue;
            const std::wstring full = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) DeleteTree(full, nullptr, false);
                RemoveDirectoryW(LongPath(full).c_str());
            } else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    SetFileAttributesW(LongPath(full).c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(LongPath(full).c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (removeSelf) RemoveDirectoryW(LongPath(dir).c_str());
}

// Short exclusive lock: a file nobody else can open while we hold it, gone when we let go (or die).
bool AcquireLock(const std::wstring& path, unsigned timeoutMs, Handle& lock) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        Handle h(CreateFileW(LongPath(path).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
        if (h.Valid()) {
            lock.Take(h);
            return true;
        }
        if (GetTickCount64() >= deadline) return false;
        Sleep(15);
    }
}

// The full, link-free path of an existing folder ("C:\\dev\\site", no trailing slash).
bool CanonicalDir(const std::wstring& input, std::wstring& out) {
    out.clear();
    if (input.empty()) return false;
    wchar_t stack[1024];
    std::wstring full;
    DWORD n = GetFullPathNameW(input.c_str(), DWORD(std::size(stack)), stack, nullptr);
    if (n == 0) return false;
    if (n < std::size(stack)) {
        full.assign(stack, n);
    } else {
        full.resize(n);
        n = GetFullPathNameW(input.c_str(), n, full.data(), nullptr);
        if (n == 0 || n >= full.size() + 1) return false;
        full.resize(n);
    }
    Handle dir(CreateFileW(LongPath(full).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!dir.Valid()) return false;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(dir.h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    std::wstring final(32768, L'\0');
    const DWORD len = GetFinalPathNameByHandleW(dir.h, final.data(), DWORD(final.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (len == 0 || len >= final.size()) {
        out = full;  // a volume without a DOS name: keep what the caller gave
    } else {
        final.resize(len);
        if (final.rfind(L"\\\\?\\UNC\\", 0) == 0) out = L"\\\\" + final.substr(8);
        else if (final.rfind(L"\\\\?\\", 0) == 0) out = final.substr(4);
        else out = final;
    }
    while (out.size() > 3 && out.back() == L'\\') out.pop_back();
    return !out.empty();
}
bool IsInside(const std::wstring& child, const std::wstring& parent) {  // child == parent, or below it
    if (child.size() < parent.size()) return false;
    if (CompareStringOrdinal(child.c_str(), int(parent.size()), parent.c_str(), int(parent.size()), TRUE) != CSTR_EQUAL)
        return false;
    return child.size() == parent.size() || child[parent.size()] == L'\\' || parent.back() == L'\\';
}

// ---------------------------------------------------------------- .gitignore
// Glob over lower-cased bytes. `*` and `?` never cross a '/'. `steps` bounds a hostile pattern.
bool Glob(const char* pb, const char* p, const char* pe, const char* s, const char* se, int& steps) {
    while (p < pe) {
        if (++steps > 20000) return false;
        const char c = *p;
        if (c == '*') {
            if (p + 1 < pe && p[1] == '*') {
                const bool atStart = p == pb || p[-1] == '/';
                const char* q = p + 2;
                if (atStart && q == pe) return true;  // "dir/**": everything inside
                if (atStart && *q == '/') {           // "**/": zero or more folders
                    ++q;
                    for (const char* t = s;;) {
                        if (Glob(pb, q, pe, t, se, steps)) return true;
                        const char* slash = static_cast<const char*>(std::memchr(t, '/', std::size_t(se - t)));
                        if (!slash) return false;
                        t = slash + 1;
                    }
                }
            }
            while (p < pe && *p == '*') ++p;  // any other run of stars is one star
            for (const char* t = s;; ++t) {
                if (Glob(pb, p, pe, t, se, steps)) return true;
                if (t == se || *t == '/') return false;
            }
        }
        if (s == se) return false;
        if (c == '?') {
            if (*s == '/') return false;
            ++s;
            while (s < se && (static_cast<unsigned char>(*s) & 0xC0) == 0x80) ++s;  // one whole UTF-8 character
            ++p;
            continue;
        }
        if (c == '[') {
            const char* q = p + 1;
            bool negate = false;
            if (q < pe && (*q == '!' || *q == '^')) {
                negate = true;
                ++q;
            }
            const char* close = q;
            if (close < pe && *close == ']') ++close;  // a leading ] is a literal
            while (close < pe && *close != ']') ++close;
            if (close < pe) {
                bool hit = false;
                for (const char* r = q; r < close; ++r) {
                    char lo = *r;
                    if (lo == '\\' && r + 1 < close) lo = *++r;
                    char hi = lo;
                    if (r + 2 < close && r[1] == '-') {
                        hi = r[2];
                        r += 2;
                    }
                    if (static_cast<unsigned char>(*s) >= static_cast<unsigned char>(lo) &&
                        static_cast<unsigned char>(*s) <= static_cast<unsigned char>(hi))
                        hit = true;
                }
                if (hit == negate || *s == '/') return false;
                ++s;
                p = close + 1;
                continue;
            }
            // no closing bracket: '[' is an ordinary character
        }
        char literal = c;
        if (c == '\\' && p + 1 < pe) literal = *++p;
        if (*s != literal) return false;
        ++s;
        ++p;
    }
    return s == se;
}

struct IgnoreRule {
    std::string pattern;  // lower-cased, leading / removed, trailing / removed
    bool negate = false;
    bool dirOnly = false;
    bool anchored = false;  // matched against the path from the .gitignore's folder; otherwise against the name
    enum Fast { Full, Literal, Suffix } fast = Full;
};
struct IgnoreFile {
    std::string base;  // "" or "sub/dir/": the folder holding this .gitignore
    std::vector<IgnoreRule> rules;

    // 0 = no rule matches, 1 = ignored, -1 = re-included. `sub` and `name` are lower-cased.
    int Verdict(std::string_view sub, std::string_view name, bool isDir) const {
        long total = 0;  // SEC-27: all the wildcard rules of one file share a budget per path (a hostile file stays cheap)
        for (std::size_t i = rules.size(); i-- > 0;) {
            const IgnoreRule& r = rules[i];
            if (r.dirOnly && !isDir) continue;
            bool match = false;
            if (r.fast == IgnoreRule::Literal) {
                match = name == r.pattern;
            } else if (r.fast == IgnoreRule::Suffix) {
                match = name.size() >= r.pattern.size() - 1 && EndsWith(name, std::string_view(r.pattern).substr(1));
            } else {
                if (total > 1000000) continue;
                const std::string_view subject = r.anchored ? sub : name;
                int steps = 0;
                match = Glob(r.pattern.data(), r.pattern.data(), r.pattern.data() + r.pattern.size(), subject.data(),
                             subject.data() + subject.size(), steps);
                total += steps;
            }
            if (match) return r.negate ? -1 : 1;
        }
        return 0;
    }
};

IgnoreFile ParseGitignore(std::string_view text, std::string base) {
    IgnoreFile file;
    file.base = std::move(base);
    if (StartsWith(text, "\xEF\xBB\xBF")) text.remove_prefix(3);
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string line(text.substr(pos, end - pos));
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        while (!line.empty() && line.back() == ' ' && !(line.size() >= 2 && line[line.size() - 2] == '\\')) line.pop_back();
        IgnoreRule rule;
        if (!line.empty() && line[0] == '!') {
            rule.negate = true;
            line.erase(0, 1);
        }
        if (!line.empty() && line.back() == '/') {
            rule.dirOnly = true;
            line.pop_back();
        }
        if (line.empty()) continue;
        rule.anchored = line.find('/') != std::string::npos;
        if (line[0] == '/') line.erase(0, 1);
        if (line.empty() || !SJ::ValidUtf8(line)) continue;
        rule.pattern = LowerAscii(line);
        // SEC-27: "**/**/" is "**/" (each one was a level of recursion in Glob); an over-long rule is no real rule.
        for (std::size_t at = 0; (at = rule.pattern.find("**/**/", at)) != std::string::npos;) {
            if (at == 0 || rule.pattern[at - 1] == '/') rule.pattern.erase(at, 3);
            else ++at;
        }
        if (rule.pattern.size() > 1024) continue;
        const bool special = rule.pattern.find_first_of("*?[\\") != std::string::npos;
        if (!rule.anchored && !special) rule.fast = IgnoreRule::Literal;
        else if (!rule.anchored && rule.pattern[0] == '*' && rule.pattern.find_first_of("*?[\\", 1) == std::string::npos)
            rule.fast = IgnoreRule::Suffix;
        file.rules.push_back(std::move(rule));
        if (file.rules.size() >= 4000) break;
    }
    return file;
}

// Deepest .gitignore first; inside one file the last matching line wins.
bool IgnoredByStack(const std::vector<IgnoreFile>& stack, std::string_view relLower, std::string_view nameLower, bool isDir) {
    for (std::size_t i = stack.size(); i-- > 0;) {
        const IgnoreFile& f = stack[i];
        const int verdict = f.Verdict(relLower.substr(f.base.size()), nameLower, isDir);
        if (verdict) return verdict > 0;
    }
    return false;
}

// ---------------------------------------------------------------- the folder's recorded state
struct Entry {
    std::string path;
    std::uint64_t size = 0, mtime = 0;
    std::string hash;  // 64 hex; empty when the content is not kept
    NotKept notKept = NotKept::No;
    bool racy = false;  // date too close to the reading: read again next time
};
struct Index {
    std::vector<Entry> files;       // sorted by path
    std::vector<std::string> dirs;  // sorted
};

char NotKeptCode(NotKept n) {
    switch (n) {
        case NotKept::TooLarge: return 'L';
        case NotKept::Secret: return 'S';
        case NotKept::Unreadable: return 'U';
        default: return 0;
    }
}
bool NotKeptFromCode(char c, NotKept& n) {
    if (c == 'L') n = NotKept::TooLarge;
    else if (c == 'S') n = NotKept::Secret;
    else if (c == 'U') n = NotKept::Unreadable;
    else return false;
    return true;
}
bool NotKeptFromName(std::string_view name, NotKept& n) {
    if (name == "too-large") n = NotKept::TooLarge;
    else if (name == "secret") n = NotKept::Secret;
    else if (name == "unreadable") n = NotKept::Unreadable;
    else return false;
    return true;
}

std::string SerializeIndex(const Index& index) {
    std::string out;
    out.reserve(64 + index.files.size() * 120 + index.dirs.size() * 40);
    out += IndexHeader;
    out += '\n';
    char number[64];
    for (const auto& d : index.dirs) {
        out += "D\t";
        out += d;
        out += '\n';
    }
    for (const auto& e : index.files) {
        out += "F\t";
        if (e.notKept == NotKept::No) out += e.hash;
        else out += NotKeptCode(e.notKept);
        std::snprintf(number, sizeof number, "\t%llu\t%llu\t%d\t", static_cast<unsigned long long>(e.size),
                      static_cast<unsigned long long>(e.mtime), e.racy ? 1 : 0);
        out += number;
        out += e.path;
        out += '\n';
    }
    return out;
}

bool ParseUnsigned(std::string_view s, std::uint64_t& out) {
    if (s.empty() || s.size() > 20) return false;
    out = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        const std::uint64_t next = out * 10 + std::uint64_t(c - '0');
        if (next < out) return false;
        out = next;
    }
    return true;
}

// Strict: one bad line and the whole index is treated as absent (everything is read again; nothing is lost).
bool ParseIndex(std::string_view text, Index& index) {
    index = Index();
    std::size_t pos = text.find('\n');
    if (pos == std::string_view::npos || text.substr(0, pos) != IndexHeader) return false;
    ++pos;
    while (pos < text.size()) {
        const std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) return false;  // truncated
        const std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (line.size() < 3 || line[1] != '\t') return false;
        if (line[0] == 'D') {
            const std::string_view path = line.substr(2);
            if (!IsSafeRelPath(path)) return false;
            index.dirs.emplace_back(path);
            continue;
        }
        if (line[0] != 'F') return false;
        std::string_view fields[5];
        std::size_t at = 2;
        for (int i = 0; i < 4; ++i) {
            const std::size_t tab = line.find('\t', at);
            if (tab == std::string_view::npos) return false;
            fields[i] = line.substr(at, tab - at);
            at = tab + 1;
        }
        fields[4] = line.substr(at);
        Entry e;
        if (fields[0].size() == 1) {
            if (!NotKeptFromCode(fields[0][0], e.notKept)) return false;
        } else if (IsHash(fields[0])) {
            e.hash = std::string(fields[0]);
        } else {
            return false;
        }
        if (!ParseUnsigned(fields[1], e.size) || !ParseUnsigned(fields[2], e.mtime)) return false;
        if (fields[3] != "0" && fields[3] != "1") return false;
        e.racy = fields[3] == "1";
        if (!IsSafeRelPath(fields[4])) return false;
        e.path = std::string(fields[4]);
        index.files.push_back(std::move(e));
    }
    const auto byPath = [](const Entry& a, const Entry& b) { return a.path < b.path; };
    if (!std::is_sorted(index.files.begin(), index.files.end(), byPath)) std::sort(index.files.begin(), index.files.end(), byPath);
    if (!std::is_sorted(index.dirs.begin(), index.dirs.end())) std::sort(index.dirs.begin(), index.dirs.end());
    return true;
}

bool SameIndex(const Index& a, const Index& b) {
    if (a.files.size() != b.files.size() || a.dirs != b.dirs) return false;
    for (std::size_t i = 0; i < a.files.size(); ++i) {
        const Entry& x = a.files[i];
        const Entry& y = b.files[i];
        if (x.size != y.size || x.mtime != y.mtime || x.racy != y.racy || x.notKept != y.notKept || x.hash != y.hash ||
            x.path != y.path)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------- blobs
std::wstring BlobPath(const std::wstring& blobsDir, const std::string& hash) {
    return blobsDir + L"\\" + Widen(hash.substr(0, 2)) + L"\\" + Widen(hash.substr(2));
}
bool BlobPresent(const std::wstring& blobsDir, const std::string& hash, std::uint64_t size) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(LongPath(BlobPath(blobsDir, hash)).c_str(), GetFileExInfoStandard, &data)) return false;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    return ((std::uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow) == size;
}
// Keeps `data` under its hash. Already there with the right size: nothing to do (that is the deduplication).
bool StoreBlob(const std::wstring& blobsDir, const std::string& hash, std::string_view data, bool flush, bool* written) {
    if (written) *written = false;
    const std::wstring final = BlobPath(blobsDir, hash);
    WIN32_FILE_ATTRIBUTE_DATA existing;
    if (GetFileAttributesExW(LongPath(final).c_str(), GetFileExInfoStandard, &existing)) {
        if (((std::uint64_t(existing.nFileSizeHigh) << 32) | existing.nFileSizeLow) == data.size()) return true;
        DeleteFileW(LongPath(final).c_str());  // wrong size: a write cut by a power loss; it is written again
    }
    const std::wstring dir = ParentOf(final);
    if (!CreateDirectoryW(LongPath(dir).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS && !EnsureDir(dir))
        return false;
    const std::wstring temp = final + L".tmp-" + Widen(RandomSuffix().substr(0, 8));
    if (!WriteNew(temp, data, flush)) return false;
    if (MoveReplace(temp, final, false, flush)) {
        if (written) *written = true;
        return true;
    }
    DeleteFileW(LongPath(temp).c_str());
    return BlobPresent(blobsDir, hash, data.size());  // another thread or process stored the same content first
}
// Reads a content back and checks it against its name: a damaged blob is never restored.
bool ReadBlob(const std::wstring& blobsDir, const std::string& hash, std::string& out) {
    if (!IsHash(hash)) return false;
    if (ReadWhole(BlobPath(blobsDir, hash), ~0ull >> 1, out) != ReadStatus::Ok) return false;
    std::string actual;
    return Sha256Hex(out, actual) && actual == hash;
}

// ---------------------------------------------------------------- recording the folder
struct ScanOutcome {
    Status status = Status::Ok;
    std::string detail;
};

struct Scanner {
    const Options& options;
    std::wstring root;       // canonical project folder
    std::wstring storeRoot;  // canonical store root (skipped if it sits inside the project)
    std::wstring blobsDir;
    std::unordered_set<std::string> excluded;
    std::vector<IgnoreFile> ignores;
    std::vector<Entry> found;
    std::vector<std::string> dirs;
    ScanStats stats;
    bool tooMany = false;
    DWORD rootError = 0;

    Scanner(const Options& o, std::wstring projectRoot, std::wstring store, std::wstring blobs)
        : options(o), root(std::move(projectRoot)), storeRoot(std::move(store)), blobsDir(std::move(blobs)) {
        for (const auto& name : options.excludedDirectories) excluded.insert(LowerAscii(name));
    }

    void Walk(const std::wstring& dir, const std::string& rel, int depth) {
        struct Item {
            std::wstring name;
            DWORD attrs;
            std::uint64_t size, mtime;
        };
        std::vector<Item> items;
        WIN32_FIND_DATAW fd;
        const HANDLE h = FindFirstFileExW(LongPath(dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                          nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) {
            if (depth == 0) rootError = GetLastError() ? GetLastError() : ERROR_PATH_NOT_FOUND;
            return;  // a sub-folder we cannot list is left out
        }
        bool hasGitignore = false;
        do {
            const wchar_t* n = fd.cFileName;
            if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0))) continue;
            items.push_back({n, fd.dwFileAttributes, (std::uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow,
                             Ticks(fd.ftLastWriteTime)});
            if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                std::wcscmp(n, L".gitignore") == 0)
                hasGitignore = true;
        } while (FindNextFileW(h, &fd));
        FindClose(h);

        bool pushed = false;
        if (hasGitignore && options.respectGitignore) {
            std::string text;
            if (ReadWhole(dir + L"\\.gitignore", MaxGitignoreBytes, text) == ReadStatus::Ok) {
                ignores.push_back(ParseGitignore(text, LowerAscii(rel)));
                pushed = true;
            }
        }
        std::string name, relPath, relLower, nameLower;
        for (const Item& item : items) {
            if (tooMany) break;
            if (!ToUtf8(item.name, name)) {
                ++stats.skippedLinks;  // a name that is not valid Unicode cannot be recorded
                continue;
            }
            nameLower = LowerAscii(name);
            if (item.attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
                ++stats.skippedLinks;  // symbolic link, junction, cloud placeholder: never followed
                continue;
            }
            // A .git folder is never recorded, whatever the options say; neither is a name our records could not
            // hold (control characters, a trailing dot or space, made outside Win32).
            if (!IsSafeComponent(name) || rel.size() + name.size() > MaxRelPathBytes) {
                if (nameLower != ".git") ++stats.skippedLinks;
                continue;
            }
            relPath.assign(rel).append(name);
            relLower = LowerAscii(relPath);
            if (item.attrs & FILE_ATTRIBUTE_DIRECTORY) {
                if (excluded.count(nameLower)) continue;
                if (!ignores.empty() && IgnoredByStack(ignores, relLower, nameLower, true)) continue;
                const std::wstring full = dir + L"\\" + item.name;
                if (!storeRoot.empty() && SameNoCaseW(full, storeRoot)) continue;
                if (depth + 1 >= MaxDepth) continue;
                dirs.push_back(relPath);
                Walk(full, relPath + "/", depth + 1);
                continue;
            }
            if (item.attrs & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_DEVICE | 0x00040000 /* RECALL_ON_OPEN */ |
                              0x00400000 /* RECALL_ON_DATA_ACCESS */)) {
                ++stats.skippedLinks;  // reading it would download it
                continue;
            }
            if (item.name.size() >= std::size(TempSuffix) - 1 &&
                item.name.compare(item.name.size() - (std::size(TempSuffix) - 1), std::wstring::npos, TempSuffix) == 0)
                continue;
            if (!ignores.empty() && IgnoredByStack(ignores, relLower, nameLower, false)) continue;
            Entry e;
            e.path = relPath;
            e.size = item.size;
            e.mtime = item.mtime;
            if (options.skipSecretFiles && MayHoldSecrets(name)) e.notKept = NotKept::Secret;
            else if (item.size > options.maxFileBytes) e.notKept = NotKept::TooLarge;
            found.push_back(std::move(e));
            if (found.size() > options.maxFiles) tooMany = true;
        }
        if (pushed) ignores.pop_back();
    }

    // Lists the folder, reuses what `prior` already knows (same size and date), reads the rest and keeps its
    // content. `out` is the folder's state now.
    ScanOutcome Run(const Index* prior, Index& out) {
        LARGE_INTEGER started;
        QueryPerformanceCounter(&started);
        const std::uint64_t scanTime = NowTicks();
        ScanOutcome outcome;
        Walk(root, "", 0);
        if (rootError) {
            outcome.status = Status::IoError;
            outcome.detail = "cannot list the project folder (error " + std::to_string(rootError) + ")";
            return outcome;
        }
        if (tooMany) {
            outcome.status = Status::ProjectTooLarge;
            outcome.detail = "more than " + std::to_string(options.maxFiles) + " files";
            return outcome;
        }
        std::unordered_map<std::string_view, const Entry*> known;
        if (prior) {
            known.reserve(prior->files.size() * 2);
            for (const auto& e : prior->files) known.emplace(e.path, &e);
        }
        std::vector<std::size_t> toRead;
        std::uint64_t tracked = 0;
        for (std::size_t i = 0; i < found.size(); ++i) {
            Entry& e = found[i];
            if (e.notKept != NotKept::No) continue;
            tracked += e.size;
            const auto it = known.find(e.path);
            if (it != known.end()) {
                const Entry& old = *it->second;
                if (old.notKept == NotKept::No && !old.racy && old.size == e.size && old.mtime == e.mtime) {
                    e.hash = old.hash;
                    continue;
                }
            }
            toRead.push_back(i);
        }
        if (tracked > options.maxStoreBytes) {
            outcome.status = Status::ProjectTooLarge;
            outcome.detail = "the files to keep exceed the store limit";
            return outcome;
        }
        if (!toRead.empty() && !EnsureDir(blobsDir)) {
            outcome.status = Status::IoError;
            outcome.detail = "cannot create the blob folder";
            return outcome;
        }

        // Reading + hashing + copying. Many files (first snapshot): several threads; a few: inline, flushed.
        const bool flush = toRead.size() <= 256;
        std::vector<char> gone(found.size(), 0);
        std::atomic<std::size_t> next{0}, blobs{0};
        std::atomic<std::uint64_t> bytes{0};
        std::atomic<bool> storeFailed{false};
        const auto work = [&]() {
            std::string content;
            for (;;) {
                const std::size_t job = next.fetch_add(1);
                if (job >= toRead.size()) return;
                const std::size_t i = toRead[job];
                Entry& e = found[i];
                std::uint64_t size = 0, mtime = 0;
                const ReadStatus read = ReadWhole(root + L"\\" + RelToWide(e.path), options.maxFileBytes, content, &size, &mtime);
                if (read == ReadStatus::Missing) {
                    gone[i] = 1;  // deleted between the listing and the reading
                    continue;
                }
                if (read == ReadStatus::TooLarge) {
                    e.notKept = NotKept::TooLarge;
                    e.size = size;
                    e.mtime = mtime;
                    continue;
                }
                if (read == ReadStatus::Denied || !Sha256Hex(content, e.hash)) {
                    e.notKept = NotKept::Unreadable;
                    e.hash.clear();
                    continue;
                }
                // The listing's size/date can lag behind the file itself: then trust nothing and read again next time.
                if (size != e.size || mtime != e.mtime || content.size() != size) e.racy = true;
                e.size = content.size();
                bool written = false;
                if (!StoreBlob(blobsDir, e.hash, content, flush, &written)) {
                    storeFailed = true;
                    continue;
                }
                if (written) {
                    ++blobs;
                    bytes += content.size();
                }
            }
        };
        std::size_t threads = 1;
        if (toRead.size() > 64) {
            threads = options.hashThreads ? options.hashThreads : std::max(1u, std::thread::hardware_concurrency());
            threads = std::min<std::size_t>(threads, 8);
        }
        if (threads <= 1) {
            work();
        } else {
            std::vector<std::thread> pool;
            for (std::size_t t = 1; t < threads; ++t) pool.emplace_back(work);
            work();
            for (auto& t : pool) t.join();
        }
        if (storeFailed) {
            outcome.status = Status::IoError;
            outcome.detail = "cannot write to the snapshot store";
            return outcome;
        }

        out = Index();
        out.files.reserve(found.size());
        for (std::size_t i = 0; i < found.size(); ++i) {
            if (gone[i]) continue;
            Entry& e = found[i];
            if (e.mtime + RacyTicks >= scanTime) e.racy = true;
            if (e.notKept == NotKept::No) {
                ++stats.files;
                stats.trackedBytes += e.size;
            } else {
                ++stats.notKept;
                e.hash.clear();
            }
            out.files.push_back(std::move(e));
        }
        std::sort(out.files.begin(), out.files.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
        std::sort(dirs.begin(), dirs.end());
        out.dirs = std::move(dirs);
        stats.directories = out.dirs.size();
        stats.read = toRead.size();
        stats.blobsWritten = blobs;
        stats.bytesWritten = bytes;
        stats.milliseconds = ElapsedMs(started);
        return outcome;
    }
};

// ---------------------------------------------------------------- text decoding and line diff
struct Decoded {
    std::string text;  // UTF-8 (a BOM is kept as U+FEFF so that adding one shows as a change)
    const char* encoding = "utf-8";
    bool binary = false;
    bool crlf = false, bareLf = false;
};

void AppendCodePoint(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xF0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

Decoded Decode(std::string_view bytes) {
    Decoded d;
    const auto u = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    const bool le = bytes.size() >= 2 && u(0) == 0xFF && u(1) == 0xFE;
    const bool be = bytes.size() >= 2 && u(0) == 0xFE && u(1) == 0xFF;
    if (le || be) {
        d.encoding = le ? "utf-16le" : "utf-16be";
        d.text.reserve(bytes.size());
        const std::size_t units = bytes.size() / 2;
        const auto unit = [&](std::size_t i) -> std::uint32_t {
            return le ? std::uint32_t(u(2 * i) | (u(2 * i + 1) << 8)) : std::uint32_t((u(2 * i) << 8) | u(2 * i + 1));
        };
        for (std::size_t i = 0; i < units; ++i) {
            std::uint32_t cp = unit(i);
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units && unit(i + 1) >= 0xDC00 && unit(i + 1) <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (unit(i + 1) - 0xDC00);
                ++i;
            } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
            if (cp == 0) {  // UTF-32, or not text at all
                d.binary = true;
                d.encoding = "binary";
                d.text.clear();
                return d;
            }
            AppendCodePoint(d.text, cp);
        }
        if (bytes.size() % 2) AppendCodePoint(d.text, 0xFFFD);
    } else if (std::memchr(bytes.data(), 0, std::min<std::size_t>(bytes.size(), 8000))) {
        d.binary = true;
        d.encoding = "binary";
        return d;
    } else if (SJ::ValidUtf8(bytes)) {
        d.encoding = StartsWith(bytes, "\xEF\xBB\xBF") ? "utf-8-bom" : "utf-8";
        d.text.assign(bytes);
        if (d.text.find('\0') != std::string::npos) {
            d.binary = true;
            d.encoding = "binary";
            d.text.clear();
            return d;
        }
    } else {
        // Not UTF-8: read as Windows-1252, the usual legacy encoding here. Only the DISPLAY depends on this guess;
        // what is restored is always the original bytes.
        static const std::uint16_t high[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                               0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                               0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                               0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
        d.encoding = "windows-1252";
        d.text.reserve(bytes.size() + bytes.size() / 8);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const unsigned char c = u(i);
            if (c == 0) {
                d.binary = true;
                d.encoding = "binary";
                d.text.clear();
                return d;
            }
            AppendCodePoint(d.text, c < 0x80 ? c : (c < 0xA0 ? high[c - 0x80] : c));
        }
    }
    for (std::size_t i = d.text.find('\n'); i != std::string::npos; i = d.text.find('\n', i + 1)) {
        if (i > 0 && d.text[i - 1] == '\r') d.crlf = true;
        else d.bareLf = true;
        if (d.crlf && d.bareLf) break;
    }
    return d;
}

// Each line WITH its ending, so "a\r\n" and "a\n" differ.
std::vector<std::string_view> SplitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::size_t end = nl == std::string_view::npos ? text.size() : nl + 1;
        lines.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return lines;
}

// Myers' shortest edit script on line ids. Appends '=', '-', '+' to `script`. Over the limits, the whole range is
// "all removed, all added": correct, not minimal.
void EditScript(const int* a, int n, const int* b, int m, std::vector<char>& script) {
    const auto giveUp = [&] {
        script.insert(script.end(), std::size_t(n), '-');
        script.insert(script.end(), std::size_t(m), '+');
    };
    if (n == 0 || m == 0) {
        giveUp();
        return;
    }
    const int cap = std::min(n + m, MaxDiffD);
    const int off = cap + 1;
    std::vector<int> v(std::size_t(2 * cap + 3), 0);
    std::vector<std::vector<int>> trace;
    std::int64_t steps = 0;
    int found = -1;
    for (int d = 0; d <= cap && found < 0; ++d) {
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[std::size_t(off + k - 1)] < v[std::size_t(off + k + 1)])) x = v[std::size_t(off + k + 1)];
            else x = v[std::size_t(off + k - 1)] + 1;
            int y = x - k;
            while (x < n && y < m && a[x] == b[y]) {
                ++x;
                ++y;
                ++steps;
            }
            v[std::size_t(off + k)] = x;
            if (x >= n && y >= m) {
                found = d;
                break;
            }
        }
        trace.emplace_back(v.begin() + (off - d), v.begin() + (off + d + 1));  // V[-d..d] after round d
        if (steps > DiffBudget) break;
    }
    if (found < 0) {
        giveUp();
        return;
    }
    std::vector<char> reversed;
    int x = n, y = m;
    for (int d = found; d > 0; --d) {
        const std::vector<int>& prev = trace[std::size_t(d - 1)];  // V[-(d-1)..(d-1)]
        const auto at = [&](int k) { return prev[std::size_t(k + d - 1)]; };
        const int k = x - y;
        const int prevK = (k == -d || (k != d && at(k - 1) < at(k + 1))) ? k + 1 : k - 1;
        const int prevX = at(prevK);
        const int prevY = prevX - prevK;
        while (x > prevX && y > prevY) {
            reversed.push_back('=');
            --x;
            --y;
        }
        if (x == prevX) {
            reversed.push_back('+');
            --y;
        } else {
            reversed.push_back('-');
            --x;
        }
    }
    while (x > 0 && y > 0) {
        reversed.push_back('=');
        --x;
        --y;
    }
    script.insert(script.end(), reversed.rbegin(), reversed.rend());
}

std::string_view StripEnding(std::string_view line) {
    if (!line.empty() && line.back() == '\n') line.remove_suffix(1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}

FileDiff DiffDecoded(std::string_view path, const Decoded* before, const Decoded* after, bool wantUnified, std::size_t maxBytes) {
    FileDiff diff;
    diff.path = std::string(path);
    diff.kind = !before ? ChangeKind::Created : (!after ? ChangeKind::Deleted : ChangeKind::Modified);
    diff.encodingBefore = before ? before->encoding : "";
    diff.encodingAfter = after ? after->encoding : "";
    if ((before && before->binary) || (after && after->binary)) {
        diff.binary = true;
        return diff;
    }
    if (before && after)
        diff.lineEndingsChanged = (before->crlf && !after->crlf && after->bareLf) || (after->crlf && !before->crlf && before->bareLf);
    static const std::string empty;
    const std::vector<std::string_view> a = SplitLines(before ? before->text : empty);
    const std::vector<std::string_view> b = SplitLines(after ? after->text : empty);

    // Common head and tail first: most edits touch a few lines of a long file.
    std::size_t head = 0;
    while (head < a.size() && head < b.size() && a[head] == b[head]) ++head;
    std::size_t tail = 0;
    while (tail < a.size() - head && tail < b.size() - head && a[a.size() - 1 - tail] == b[b.size() - 1 - tail]) ++tail;
    const std::size_t an = a.size() - head - tail, bn = b.size() - head - tail;
    std::vector<char> script(head, '=');
    if (an || bn) {
        std::unordered_map<std::string_view, int> ids;
        ids.reserve((an + bn) * 2);
        std::vector<int> ai(an), bi(bn);
        for (std::size_t i = 0; i < an; ++i) ai[i] = ids.emplace(a[head + i], int(ids.size())).first->second;
        for (std::size_t i = 0; i < bn; ++i) bi[i] = ids.emplace(b[head + i], int(ids.size())).first->second;
        EditScript(ai.data(), int(an), bi.data(), int(bn), script);
    }
    script.insert(script.end(), tail, '=');
    for (const char op : script) {
        if (op == '+') ++diff.additions;
        else if (op == '-') ++diff.deletions;
    }
    if (!wantUnified || (diff.additions == 0 && diff.deletions == 0)) return diff;

    std::string& out = diff.unified;
    out += before ? "--- a/" + diff.path + "\n" : std::string("--- /dev/null\n");
    out += after ? "+++ b/" + diff.path + "\n" : std::string("+++ /dev/null\n");
    constexpr std::size_t Context = 3;
    // Line numbers (0-based) in a and b at each script position.
    std::vector<std::uint32_t> ax(script.size() + 1), bx(script.size() + 1);
    for (std::size_t i = 0; i < script.size(); ++i) {
        ax[i + 1] = ax[i] + (script[i] != '+' ? 1 : 0);
        bx[i + 1] = bx[i] + (script[i] != '-' ? 1 : 0);
    }
    std::size_t i = 0;
    while (i < script.size() && !diff.truncated) {
        if (script[i] == '=') {
            ++i;
            continue;
        }
        std::size_t last = i;  // last changed position of this hunk
        for (std::size_t j = i + 1; j < script.size() && j - last <= 2 * Context + 1; ++j)
            if (script[j] != '=') last = j;
        const std::size_t from = i >= Context ? i - Context : 0;
        const std::size_t to = std::min(script.size(), last + 1 + Context);
        const std::uint32_t aCount = ax[to] - ax[from], bCount = bx[to] - bx[from];
        char header[96];
        std::snprintf(header, sizeof header, "@@ -%u,%u +%u,%u @@\n", aCount ? ax[from] + 1 : ax[from], aCount,
                      bCount ? bx[from] + 1 : bx[from], bCount);
        out += header;
        for (std::size_t j = from; j < to; ++j) {
            const char op = script[j];
            std::string_view line = op == '+' ? b[bx[j]] : a[ax[j]];
            const bool noNewline = line.empty() || line.back() != '\n';
            line = StripEnding(line);
            const bool first = op == '+' ? bx[j] == 0 : ax[j] == 0;
            if (first && StartsWith(line, "\xEF\xBB\xBF")) line.remove_prefix(3);
            const std::size_t mark = out.size();
            out += op == '=' ? ' ' : op;
            out += line;
            out += '\n';
            if (noNewline) out += "\\ No newline at end of file\n";
            if (out.size() > maxBytes) {
                out.resize(mark);
                diff.truncated = true;
                break;
            }
        }
        i = to;
    }
    return diff;
}

// ---------------------------------------------------------------- turn records
struct State {
    enum Kind { Absent, Content, Opaque } kind = Absent;
    std::string hash;        // Content
    std::uint64_t size = 0;  // Content, Opaque
    std::uint64_t mtime = 0;  // Opaque: with the size, all we know of it
    NotKept notKept = NotKept::No;
};
State StateOf(const Entry* e) {
    State s;
    if (!e) return s;
    s.size = e->size;
    if (e->notKept == NotKept::No) {
        s.kind = State::Content;
        s.hash = e->hash;
    } else {
        s.kind = State::Opaque;
        s.mtime = e->mtime;
        s.notKept = e->notKept;
    }
    return s;
}
bool SameState(const State& a, const State& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == State::Absent) return true;
    if (a.kind == State::Content) return a.hash == b.hash;
    return a.size == b.size && a.mtime == b.mtime;
}

struct ChangeRecord {
    std::string path;
    State before, after;
    bool binary = false;
    int additions = 0, deletions = 0;
    std::string renamedFrom, renamedTo;
    ChangeKind Kind() const {
        if (before.kind == State::Absent) return ChangeKind::Created;
        if (after.kind == State::Absent) return ChangeKind::Deleted;
        return ChangeKind::Modified;
    }
};
struct TurnRecord {
    std::string id, checkpoint;
    std::uint64_t seq = 0;
    std::int64_t startedAt = 0, endedAt = 0;
    bool interrupted = false, undone = false;
    std::vector<ChangeRecord> changes;
    std::vector<std::string> createdDirs;
};

Value StateJson(const State& s) {
    if (s.kind == State::Absent) return Value::Null();
    Value v = Value::Obj();
    if (s.kind == State::Content) {
        v.Set("hash", Value::Str(s.hash));
        v.Set("size", Value::Int(std::int64_t(s.size)));
    } else {
        v.Set("notKept", Value::Str(NotKeptName(s.notKept)));
        v.Set("size", Value::Int(std::int64_t(s.size)));
        v.Set("mtime", Value::Int(std::int64_t(s.mtime)));
    }
    return v;
}
bool GetUnsigned(const Value& object, std::string_view key, std::uint64_t& out) {
    const Value* v = object.Get(key);
    if (!v || !v->IsNumber() || !v->integral || v->integer < 0) return false;
    out = std::uint64_t(v->integer);
    return true;
}
bool GetString(const Value& object, std::string_view key, std::string& out) {
    const Value* v = object.Get(key);
    if (!v || !v->IsString()) return false;
    out = v->string;
    return true;
}
bool GetBool(const Value& object, std::string_view key, bool& out) {
    const Value* v = object.Get(key);
    if (!v || !v->IsBool()) return false;
    out = v->boolean;
    return true;
}
bool StateFromJson(const Value* v, State& s) {
    s = State();
    if (!v) return false;
    if (v->IsNull()) return true;
    if (!v->IsObject()) return false;
    if (!GetUnsigned(*v, "size", s.size)) return false;
    std::string text;
    if (GetString(*v, "hash", text)) {
        if (!IsHash(text)) return false;
        s.kind = State::Content;
        s.hash = text;
        return true;
    }
    if (!GetString(*v, "notKept", text) || !NotKeptFromName(text, s.notKept)) return false;
    s.kind = State::Opaque;
    return GetUnsigned(*v, "mtime", s.mtime);
}

std::string TurnJson(const TurnRecord& t) {
    Value v = Value::Obj();
    v.Set("schema", Value::Str(TurnSchema));
    v.Set("id", Value::Str(t.id));
    v.Set("checkpoint", Value::Str(t.checkpoint));
    v.Set("seq", Value::Int(std::int64_t(t.seq)));
    v.Set("startedAt", Value::Int(t.startedAt));
    v.Set("endedAt", Value::Int(t.endedAt));
    v.Set("interrupted", Value::Bool(t.interrupted));
    v.Set("undone", Value::Bool(t.undone));
    Value changes = Value::Arr();
    for (const auto& c : t.changes) {
        Value item = Value::Obj();
        item.Set("path", Value::Str(c.path));
        item.Set("kind", Value::Str(ChangeKindName(c.Kind())));
        item.Set("before", StateJson(c.before));
        item.Set("after", StateJson(c.after));
        item.Set("binary", Value::Bool(c.binary));
        item.Set("additions", Value::Int(c.additions));
        item.Set("deletions", Value::Int(c.deletions));
        if (!c.renamedFrom.empty()) item.Set("renamedFrom", Value::Str(c.renamedFrom));
        if (!c.renamedTo.empty()) item.Set("renamedTo", Value::Str(c.renamedTo));
        changes.Push(std::move(item));
    }
    v.Set("changes", std::move(changes));
    Value dirs = Value::Arr();
    for (const auto& d : t.createdDirs) dirs.Push(Value::Str(d));
    v.Set("createdDirs", std::move(dirs));
    return SJ::Serialize(v) + "\n";
}

SJ::Limits RecordLimits() {
    SJ::Limits limits;
    limits.maxBytes = 256u << 20;
    limits.maxItems = 8u << 20;
    return limits;
}

bool ParseTurn(std::string_view text, TurnRecord& t) {
    Value v;
    if (!SJ::Parse(text, v, nullptr, RecordLimits()) || !v.IsObject()) return false;
    std::string schema;
    if (!GetString(v, "schema", schema) || schema != TurnSchema) return false;
    std::uint64_t started = 0, ended = 0;
    if (!GetString(v, "id", t.id) || !IsId(t.id, "trn_")) return false;
    if (!GetString(v, "checkpoint", t.checkpoint) || !IsId(t.checkpoint, "chk_")) return false;
    if (!GetUnsigned(v, "seq", t.seq) || t.seq == 0) return false;
    if (!GetUnsigned(v, "startedAt", started) || !GetUnsigned(v, "endedAt", ended)) return false;
    t.startedAt = std::int64_t(started);
    t.endedAt = std::int64_t(ended);
    if (!GetBool(v, "interrupted", t.interrupted) || !GetBool(v, "undone", t.undone)) return false;
    const Value* changes = v.Get("changes");
    if (!changes || !changes->IsArray()) return false;
    t.changes.clear();
    for (const Value& item : changes->items) {
        if (!item.IsObject()) return false;
        ChangeRecord c;
        if (!GetString(item, "path", c.path) || !IsSafeRelPath(c.path)) return false;
        if (!StateFromJson(item.Get("before"), c.before) || !StateFromJson(item.Get("after"), c.after)) return false;
        if (c.before.kind == State::Absent && c.after.kind == State::Absent) return false;
        std::uint64_t add = 0, del = 0;
        if (!GetBool(item, "binary", c.binary) || !GetUnsigned(item, "additions", add) || !GetUnsigned(item, "deletions", del))
            return false;
        c.additions = int(std::min<std::uint64_t>(add, 0x7fffffff));
        c.deletions = int(std::min<std::uint64_t>(del, 0x7fffffff));
        GetString(item, "renamedFrom", c.renamedFrom);
        GetString(item, "renamedTo", c.renamedTo);
        t.changes.push_back(std::move(c));
    }
    const Value* dirs = v.Get("createdDirs");
    if (!dirs || !dirs->IsArray()) return false;
    t.createdDirs.clear();
    for (const Value& d : dirs->items) {
        if (!d.IsString() || !IsSafeRelPath(d.string)) return false;
        t.createdDirs.push_back(d.string);
    }
    return true;
}

TurnSummary Summarize(const TurnRecord& t) {
    TurnSummary s;
    s.turnId = t.id;
    s.checkpointId = t.checkpoint;
    s.sequence = t.seq;
    s.startedAt = t.startedAt;
    s.endedAt = t.endedAt;
    s.interrupted = t.interrupted;
    s.undone = t.undone;
    for (const auto& c : t.changes) {
        FileChange f;
        f.path = c.path;
        f.kind = c.Kind();
        f.binary = c.binary;
        f.additions = c.additions;
        f.deletions = c.deletions;
        f.restorable = c.before.kind != State::Opaque;
        f.notKept = c.before.kind == State::Opaque ? c.before.notKept : (c.after.kind == State::Opaque ? c.after.notKept : NotKept::No);
        f.renamedFrom = c.renamedFrom;
        f.renamedTo = c.renamedTo;
        s.additions += c.additions;
        s.deletions += c.deletions;
        s.changes.push_back(std::move(f));
    }
    return s;
}

// ---------------------------------------------------------------- restore journal
struct RestoreOp {
    std::string path;
    State target;                // Absent (remove the file) or Content (put this content back)
    bool fromExists = false;     // what stood there when the restore was decided
    std::uint64_t fromSize = 0, fromMtime = 0;
    std::string fromHash;        // empty when it was not read
    std::string temp;            // leaf name of our temporary file, next to the target
};
struct Journal {
    std::string id;
    std::vector<std::string> turns;
    std::vector<std::string> removeDirs;  // folders the undone turns created; removed when left empty
    std::vector<RestoreOp> ops;           // removals first, then writes
};

std::string JournalJson(const Journal& j) {
    Value v = Value::Obj();
    v.Set("schema", Value::Str(RestoreSchema));
    v.Set("id", Value::Str(j.id));
    Value turns = Value::Arr();
    for (const auto& t : j.turns) turns.Push(Value::Str(t));
    v.Set("turns", std::move(turns));
    Value dirs = Value::Arr();
    for (const auto& d : j.removeDirs) dirs.Push(Value::Str(d));
    v.Set("removeDirs", std::move(dirs));
    Value ops = Value::Arr();
    for (const auto& op : j.ops) {
        Value item = Value::Obj();
        item.Set("path", Value::Str(op.path));
        item.Set("target", StateJson(op.target));
        if (op.fromExists) {
            Value from = Value::Obj();
            from.Set("size", Value::Int(std::int64_t(op.fromSize)));
            from.Set("mtime", Value::Int(std::int64_t(op.fromMtime)));
            from.Set("hash", Value::Str(op.fromHash));
            item.Set("from", std::move(from));
        } else {
            item.Set("from", Value::Null());
        }
        item.Set("temp", Value::Str(op.temp));
        ops.Push(std::move(item));
    }
    v.Set("ops", std::move(ops));
    return SJ::Serialize(v) + "\n";
}

bool IsTempLeaf(std::string_view name) {
    if (name.size() < 10 || name[0] != '~' || !EndsWith(name, ".snaptmp")) return false;
    for (const char c : name.substr(1, name.size() - 9))
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

bool ParseJournal(std::string_view text, Journal& j) {
    Value v;
    if (!SJ::Parse(text, v, nullptr, RecordLimits()) || !v.IsObject()) return false;
    std::string schema;
    if (!GetString(v, "schema", schema) || schema != RestoreSchema || !GetString(v, "id", j.id)) return false;
    const Value* turns = v.Get("turns");
    const Value* dirs = v.Get("removeDirs");
    const Value* ops = v.Get("ops");
    if (!turns || !turns->IsArray() || !dirs || !dirs->IsArray() || !ops || !ops->IsArray()) return false;
    for (const Value& t : turns->items) {
        if (!t.IsString() || !IsId(t.string, "trn_")) return false;
        j.turns.push_back(t.string);
    }
    for (const Value& d : dirs->items) {
        if (!d.IsString() || !IsSafeRelPath(d.string)) return false;
        j.removeDirs.push_back(d.string);
    }
    for (const Value& item : ops->items) {
        if (!item.IsObject()) return false;
        RestoreOp op;
        if (!GetString(item, "path", op.path) || !IsSafeRelPath(op.path)) return false;
        if (!StateFromJson(item.Get("target"), op.target) || op.target.kind == State::Opaque) return false;
        const Value* from = item.Get("from");
        if (!from) return false;
        if (from->IsObject()) {
            op.fromExists = true;
            if (!GetUnsigned(*from, "size", op.fromSize) || !GetUnsigned(*from, "mtime", op.fromMtime) ||
                !GetString(*from, "hash", op.fromHash) || (!op.fromHash.empty() && !IsHash(op.fromHash)))
                return false;
        } else if (!from->IsNull()) {
            return false;
        }
        if (!GetString(item, "temp", op.temp) || !IsTempLeaf(op.temp)) return false;
        j.ops.push_back(std::move(op));
    }
    return true;
}

}  // namespace

// ================================================================ public pure helpers
std::vector<std::string> DefaultExcludedDirectories() {
    return {"node_modules", ".git", "dist", "build", ".next", ".astro", ".turbo", ".svelte-kit", ".nuxt",
            ".output",      ".vercel", ".cache", ".parcel-cache", ".vite", ".hg", ".svn"};
}

const char* StatusName(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::Conflict: return "conflict";
        case Status::Partial: return "partial";
        case Status::NoProject: return "no-project";
        case Status::InvalidProject: return "invalid-project";
        case Status::ProjectTooLarge: return "project-too-large";
        case Status::TurnOpen: return "turn-open";
        case Status::NoTurnOpen: return "no-turn-open";
        case Status::NotFound: return "not-found";
        case Status::AlreadyUndone: return "already-undone";
        case Status::Busy: return "busy";
        case Status::Corrupt: return "corrupt";
        case Status::IoError: return "io-error";
    }
    return "io-error";
}
const char* ChangeKindName(ChangeKind kind) {
    switch (kind) {
        case ChangeKind::Created: return "created";
        case ChangeKind::Deleted: return "deleted";
        default: return "modified";
    }
}
const char* NotKeptName(NotKept reason) {
    switch (reason) {
        case NotKept::TooLarge: return "too-large";
        case NotKept::Secret: return "secret";
        case NotKept::Unreadable: return "unreadable";
        default: return "";
    }
}
const char* ConflictReasonName(ConflictReason reason) {
    switch (reason) {
        case ConflictReason::ModifiedSinceTurn: return "modified-since-turn";
        case ConflictReason::DeletedSinceTurn: return "deleted-since-turn";
        case ConflictReason::CreatedSinceTurn: return "created-since-turn";
        case ConflictReason::EditedBetweenTurns: return "edited-between-turns";
        case ConflictReason::InUse: return "in-use";
        case ConflictReason::Blocked: return "blocked";
        case ConflictReason::SnapshotLost: return "snapshot-lost";
    }
    return "modified-since-turn";
}

std::string ProjectKey(std::wstring_view canonicalProjectDir) {
    std::wstring lower(canonicalProjectDir);
    if (!lower.empty()) CharLowerBuffW(lower.data(), DWORD(lower.size()));
    std::string hash;
    Sha256Hex(Narrow(lower), hash);
    const std::string leaf = Narrow(LeafOf(lower));
    std::string slug;
    for (const char c : leaf) {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep) slug.push_back(c);
        else if (!slug.empty() && slug.back() != '-') slug.push_back('-');
        if (slug.size() >= 32) break;
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug = "project";
    return slug + "-" + hash.substr(0, 16);
}

FileDiff DiffContents(std::string_view path, const std::string* before, const std::string* after, bool wantUnified,
                      std::size_t maxBytes) {
    Decoded b, a;
    if (before) b = Decode(*before);
    if (after) a = Decode(*after);
    return DiffDecoded(path, before ? &b : nullptr, after ? &a : nullptr, wantUnified, maxBytes);
}

bool GitignoreIgnores(std::string_view gitignoreText, std::string_view path, bool isDirectory) {
    const IgnoreFile file = ParseGitignore(gitignoreText, "");
    const std::string lower = LowerAscii(path);
    // Walk down the path: an ignored folder ignores everything under it.
    std::size_t start = 0;
    while (start < lower.size()) {
        std::size_t end = lower.find('/', start);
        const bool last = end == std::string::npos;
        if (last) end = lower.size();
        const std::string_view sub(lower.data(), end);
        const std::string_view name(lower.data() + start, end - start);
        if (file.Verdict(sub, name, last ? isDirectory : true) > 0) return true;
        start = end + 1;
    }
    return false;
}

bool MayHoldSecrets(std::string_view fileName) {
    const std::string name = LowerAscii(fileName);
    const auto oneOf = [&](std::initializer_list<const char*> list) {
        for (const char* item : list)
            if (name == item) return true;
        return false;
    };
    const auto endsWithAny = [&](std::initializer_list<const char*> list) {
        for (const char* item : list)
            if (EndsWith(name, item)) return true;
        return false;
    };
    // Environment files (.env, .env.local, .env.production...), except the samples meant to be shared.
    if (name == ".env" || StartsWith(name, ".env.") || name == ".envrc" || name == ".dev.vars" || StartsWith(name, ".dev.vars."))
        return !endsWithAny({".example", ".sample", ".template"});
    if (EndsWith(name, ".env") && name.size() > 4) return true;
    if (endsWithAny({".pem", ".key", ".p8", ".p12", ".pfx", ".jks", ".keystore", ".ppk", ".tfvars", ".tfstate"})) return true;
    if (StartsWith(name, "id_rsa") || StartsWith(name, "id_dsa") || StartsWith(name, "id_ecdsa") || StartsWith(name, "id_ed25519"))
        return true;
    return oneOf({".npmrc", ".netrc", ".pypirc", ".git-credentials", ".pgpass", "credentials.json", "secrets.json",
                  "secrets.yaml", "secrets.yml"});
}

// ================================================================ Store
struct Store::Impl {
    std::wstring storeRootGiven;
    std::wstring storeRoot;  // canonical once the folder exists
    Options options;
    mutable std::mutex mutex;

    std::wstring projectDir;  // canonical; empty = not bound
    std::wstring projectStore;

    bool turnOpen = false;
    Handle turnLock;
    TurnRecord openTurn;  // id, checkpoint, seq, startedAt of the running turn

    std::int64_t Now() const { return options.clock ? options.clock() : SystemSeconds(); }
    bool Fault(std::string_view point) const { return options.fault && options.fault(point); }

    std::wstring BlobsDir() const { return projectStore + L"\\blobs"; }
    std::wstring TurnsDir() const { return projectStore + L"\\turns"; }
    std::wstring IndexPath() const { return projectStore + L"\\index"; }
    std::wstring MarkerPath() const { return projectStore + L"\\open-turn.json"; }
    std::wstring TurnLockPath() const { return projectStore + L"\\turn.lock"; }
    std::wstring JournalPath() const { return projectStore + L"\\restore.json"; }
    std::wstring LockPath() const { return projectStore + L"\\lock"; }
    std::wstring FullPath(std::string_view rel) const { return projectDir + L"\\" + RelToWide(rel); }

    static Result Fail(Status status, std::string detail) {
        Result r;
        r.status = status;
        r.detail = std::move(detail);
        return r;
    }

    // ------------------------------------------------------------ binding
    Result Bind(const std::wstring& dir) {
        std::wstring canonical;
        if (!CanonicalDir(dir, canonical)) return Fail(Status::InvalidProject, "not an existing folder");
        if (canonical.size() <= 3) return Fail(Status::InvalidProject, "a drive root cannot be a project");
        if (!projectDir.empty() && SameNoCaseW(canonical, projectDir)) return Result();
        if (turnOpen) return Fail(Status::TurnOpen, "a turn is running on another project of this store object");
        if (storeRoot.empty()) {
            if (!EnsureDir(storeRootGiven)) return Fail(Status::IoError, "cannot create the snapshot store");
            if (!CanonicalDir(storeRootGiven, storeRoot)) return Fail(Status::IoError, "cannot open the snapshot store");
        }
        if (IsInside(canonical, storeRoot)) return Fail(Status::InvalidProject, "the project is inside the snapshot store");
        const std::wstring store = storeRoot + L"\\" + Widen(ProjectKey(canonical));
        if (!EnsureDir(store)) return Fail(Status::IoError, "cannot create the project's snapshot folder");
        if (!FileExists(store + L"\\project.json") && !WriteProjectFile(store, canonical))
            return Fail(Status::IoError, "cannot write project.json");
        projectDir = canonical;
        projectStore = store;
        return Result();
    }
    bool WriteProjectFile(const std::wstring& store, const std::wstring& canonical) const {
        Value v = Value::Obj();
        v.Set("schema", Value::Str(ProjectSchema));
        v.Set("root", Value::Str(Narrow(canonical)));
        v.Set("createdAt", Value::Int(Now()));
        return WriteAtomic(store + L"\\project.json", SJ::Serialize(v) + "\n");
    }

    Result Lock(Handle& lock) {
        if (projectDir.empty()) return Fail(Status::NoProject, "no project bound");
        if (!EnsureDir(projectStore)) return Fail(Status::IoError, "cannot create the project's snapshot folder");
        if (!AcquireLock(LockPath(), options.lockTimeoutMs, lock)) return Fail(Status::Busy, "the project's store is busy");
        return Result();
    }

    // ------------------------------------------------------------ index and records
    bool LoadIndex(Index& index) const {
        std::string text;
        if (ReadWhole(IndexPath(), 1ull << 30, text) != ReadStatus::Ok) return false;
        return ParseIndex(text, index);
    }
    bool SaveIndex(const Index& index) const { return WriteAtomic(IndexPath(), SerializeIndex(index)); }

    std::wstring TurnPath(const TurnRecord& t) const {
        char prefix[32];
        std::snprintf(prefix, sizeof prefix, "%08llu-", static_cast<unsigned long long>(t.seq));
        return TurnsDir() + L"\\" + Widen(prefix) + Widen(t.id) + L".json";
    }
    bool SaveTurn(const TurnRecord& t) const { return WriteAtomic(TurnPath(t), TurnJson(t)); }

    // Every record, oldest first. `damaged` gets the sequence numbers of the records that cannot be read.
    void LoadTurns(std::vector<TurnRecord>& turns, std::vector<std::uint64_t>* damaged = nullptr,
                   std::uint64_t* highest = nullptr) const {
        turns.clear();
        if (highest) *highest = 0;
        WIN32_FIND_DATAW fd;
        const HANDLE h = FindFirstFileExW(LongPath(TurnsDir() + L"\\*.json").c_str(), FindExInfoBasic, &fd,
                                          FindExSearchNameMatch, nullptr, 0);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            const std::string name = Narrow(fd.cFileName);
            std::uint64_t seq = 0;
            // "<8+ digits>-trn_<26>.json"
            const std::size_t dash = name.find('-');
            if (dash == std::string::npos || !ParseUnsigned(std::string_view(name).substr(0, dash), seq) || seq == 0) continue;
            if (!EndsWith(name, ".json") || !IsId(std::string_view(name).substr(dash + 1, name.size() - dash - 6), "trn_")) continue;
            if (highest) *highest = std::max(*highest, seq);
            std::string text;
            TurnRecord t;
            if (ReadWhole(TurnsDir() + L"\\" + fd.cFileName, 256ull << 20, text) != ReadStatus::Ok || !ParseTurn(text, t) ||
                t.seq != seq) {
                if (damaged) damaged->push_back(seq);
                continue;
            }
            turns.push_back(std::move(t));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(turns.begin(), turns.end(), [](const TurnRecord& a, const TurnRecord& b) { return a.seq < b.seq; });
    }

    bool LoadMarker(TurnRecord& t) const {
        std::string text;
        if (ReadWhole(MarkerPath(), 64 * 1024, text) != ReadStatus::Ok) return false;
        Value v;
        std::string schema;
        std::uint64_t started = 0;
        if (!SJ::Parse(text, v) || !v.IsObject() || !GetString(v, "schema", schema) || schema != OpenTurnSchema) return false;
        if (!GetString(v, "id", t.id) || !IsId(t.id, "trn_") || !GetString(v, "checkpoint", t.checkpoint) ||
            !IsId(t.checkpoint, "chk_") || !GetUnsigned(v, "seq", t.seq) || t.seq == 0 || !GetUnsigned(v, "startedAt", started))
            return false;
        t.startedAt = std::int64_t(started);
        return true;
    }
    bool SaveMarker(const TurnRecord& t) const {
        Value v = Value::Obj();
        v.Set("schema", Value::Str(OpenTurnSchema));
        v.Set("id", Value::Str(t.id));
        v.Set("checkpoint", Value::Str(t.checkpoint));
        v.Set("seq", Value::Int(std::int64_t(t.seq)));
        v.Set("startedAt", Value::Int(t.startedAt));
        return WriteAtomic(MarkerPath(), SJ::Serialize(v) + "\n");
    }

    ScanOutcome Scan(const Index* prior, Index& out, ScanStats* stats) const {
        Scanner scanner(options, projectDir, storeRoot, BlobsDir());
        const ScanOutcome outcome = scanner.Run(prior, out);
        if (stats) *stats = scanner.stats;
        return outcome;
    }

    // ------------------------------------------------------------ closing a turn
    // What changed between two recorded states, with line counts.
    void ComputeChanges(const Index& before, const Index& after, TurnRecord& turn) const {
        turn.changes.clear();
        std::size_t i = 0, j = 0;
        const auto add = [&](const Entry* b, const Entry* a) {
            ChangeRecord c;
            c.path = b ? b->path : a->path;
            c.before = StateOf(b);
            c.after = StateOf(a);
            if (b && a) {
                if (SameState(c.before, c.after)) return;
                // One side not kept: same size and date means "not touched" (a locked or secret file, unchanged).
                if ((c.before.kind == State::Opaque || c.after.kind == State::Opaque) && b->size == a->size && b->mtime == a->mtime)
                    return;
            }
            turn.changes.push_back(std::move(c));
        };
        while (i < before.files.size() || j < after.files.size()) {
            if (j >= after.files.size() || (i < before.files.size() && before.files[i].path < after.files[j].path)) {
                add(&before.files[i++], nullptr);
            } else if (i >= before.files.size() || after.files[j].path < before.files[i].path) {
                add(nullptr, &after.files[j++]);
            } else {
                add(&before.files[i], &after.files[j]);
                ++i;
                ++j;
            }
        }
        const std::wstring blobs = BlobsDir();
        std::unordered_map<std::string, ChangeRecord*> deletedByHash;
        for (auto& c : turn.changes) {
            std::string before, after;
            const bool hasBefore = c.before.kind == State::Content && ReadBlob(blobs, c.before.hash, before);
            const bool hasAfter = c.after.kind == State::Content && ReadBlob(blobs, c.after.hash, after);
            const bool needBefore = c.before.kind != State::Absent, needAfter = c.after.kind != State::Absent;
            if ((needBefore && !hasBefore) || (needAfter && !hasAfter)) continue;  // a side not kept: no counts
            const FileDiff d = DiffContents(c.path, needBefore ? &before : nullptr, needAfter ? &after : nullptr, false, 0);
            c.binary = d.binary;
            c.additions = d.additions;
            c.deletions = d.deletions;
            if (c.Kind() == ChangeKind::Deleted && c.before.kind == State::Content) deletedByHash.emplace(c.before.hash, &c);
        }
        for (auto& c : turn.changes) {
            if (c.Kind() != ChangeKind::Created || c.after.kind != State::Content || c.after.size == 0) continue;
            const auto it = deletedByHash.find(c.after.hash);
            if (it == deletedByHash.end() || !it->second->renamedTo.empty()) continue;
            c.renamedFrom = it->second->path;
            it->second->renamedTo = c.path;
        }
        turn.createdDirs.clear();
        std::set_difference(after.dirs.begin(), after.dirs.end(), before.dirs.begin(), before.dirs.end(),
                            std::back_inserter(turn.createdDirs));
    }

    // Records the folder now, writes the turn's record, makes "now" the known state. Caller holds the lock.
    // Keeps only the changes of `onlyFiles` (ASCII case-insensitive): the rest was changed by someone else.
    static void KeepOnly(TurnRecord& turn, const std::vector<std::string>& onlyFiles) {
        const auto fold = [](std::string v) { for (auto& ch : v) if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a'); return v; };
        std::set<std::string> keep;
        for (const auto& f : onlyFiles) keep.insert(fold(f));
        turn.changes.erase(std::remove_if(turn.changes.begin(), turn.changes.end(), [&](const ChangeRecord& c) { return !keep.count(fold(c.path)); }), turn.changes.end());
        std::set<std::string> kept;
        for (const auto& c : turn.changes) kept.insert(c.path);
        for (auto& c : turn.changes) {
            if (!c.renamedFrom.empty() && !kept.count(c.renamedFrom)) c.renamedFrom.clear();
            if (!c.renamedTo.empty() && !kept.count(c.renamedTo)) c.renamedTo.clear();
        }
        // a folder created during the turn is the agent's only when it holds one of the agent's files
        turn.createdDirs.erase(std::remove_if(turn.createdDirs.begin(), turn.createdDirs.end(), [&](const std::string& d) {
            for (const auto& c : turn.changes) if (c.path.size() > d.size() && c.path[d.size()] == '/' && fold(c.path.substr(0, d.size())) == fold(d)) return false;
            return true; }), turn.createdDirs.end());
    }

    Result FinishTurn(TurnRecord turn, bool interrupted, TurnSummary* summary, ScanStats* stats, const std::vector<std::string>* onlyFiles = nullptr) {
        turn.interrupted = interrupted;
        turn.endedAt = std::max(Now(), turn.startedAt);
        Index before, after;
        const bool hadBefore = LoadIndex(before);
        const ScanOutcome scan = Scan(hadBefore ? &before : nullptr, after, stats);
        Result result;
        if (scan.status != Status::Ok) {
            // The folder cannot be recorded (gone, or grown past the limits): the turn is closed without a
            // record, so it cannot be undone, and the known state stays the one before it.
            DeleteFileW(LongPath(MarkerPath()).c_str());
            result = Fail(scan.status, scan.detail + "; this turn cannot be undone");
        } else {
            if (hadBefore) { ComputeChanges(before, after, turn); if (onlyFiles) KeepOnly(turn, *onlyFiles); }
            else result = Fail(Status::Corrupt, "the state before the turn was lost; this turn cannot be undone");
            if (!SaveTurn(turn)) return Fail(Status::IoError, "cannot write the turn record");
            if (Fault("end.record")) return Fail(Status::IoError, "fault injected at end.record");
            if (!SaveIndex(after)) return Fail(Status::IoError, "cannot write the index");
            if (Fault("end.index")) return Fail(Status::IoError, "fault injected at end.index");
            DeleteFileW(LongPath(MarkerPath()).c_str());
        }
        if (summary) {
            *summary = Summarize(turn);
            if (scan.status != Status::Ok) summary->checkpointId.clear();
        }
        return result;
    }

    // ------------------------------------------------------------ recovery
    // Completes a restore cut short and closes a turn whose browser died. `turnElsewhere`: a live turn of another
    // store object holds the project. Caller holds the lock.
    void Recover(Result& out, bool* turnElsewhere) {
        if (turnElsewhere) *turnElsewhere = false;
        if (FileExists(JournalPath())) ResumeRestore(out);
        if (turnOpen || !FileExists(MarkerPath())) return;
        Handle stale;
        if (!AcquireLock(TurnLockPath(), TurnLockWaitMs, stale)) {
            if (turnElsewhere) *turnElsewhere = true;
            return;
        }
        TurnRecord turn;
        if (LoadMarker(turn)) FinishTurn(turn, true, nullptr, nullptr);
        else DeleteFileW(LongPath(MarkerPath()).c_str());
    }

    // ------------------------------------------------------------ restoring
    // No ancestor of `rel` may be a link (we would write outside the project) or a file (unless `willRemove` says
    // the restore removes that file first).
    bool ParentsAreSafe(std::string_view rel, const std::function<bool(std::string_view)>& willRemove) const {
        for (std::size_t slash = rel.find('/'); slash != std::string_view::npos; slash = rel.find('/', slash + 1)) {
            const std::string_view prefix = rel.substr(0, slash);
            const DWORD attrs = GetFileAttributesW(LongPath(FullPath(prefix)).c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) return true;  // not there: it will be created, as a plain folder
            if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return false;
            if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) && !(willRemove && willRemove(prefix))) return false;
        }
        return true;
    }

    // Performs one journal entry. `detail` says why it failed.
    bool ApplyOp(const RestoreOp& op, std::string& detail) const {
        const std::wstring full = FullPath(op.path);
        if (op.target.kind == State::Absent) {
            if (DeleteFileW(LongPath(full).c_str())) return true;
            const DWORD e = GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;
            detail = "cannot remove (error " + std::to_string(e) + ")";
            return false;
        }
        std::string content;
        if (!ReadBlob(BlobsDir(), op.target.hash, content)) {
            detail = "the kept content is missing or damaged";
            return false;
        }
        const std::wstring parent = ParentOf(full);
        if (!EnsureDir(parent)) {
            detail = "cannot create the folder";
            return false;
        }
        const std::wstring temp = parent + L"\\" + Widen(op.temp);
        DeleteFileW(LongPath(temp).c_str());  // left by an earlier, interrupted attempt
        if (!WriteNew(temp, content, true)) {
            detail = "cannot write the temporary file (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        if (Fault("restore.temp")) {
            detail = "fault";
            return false;
        }
        if (!MoveReplace(temp, full, true, true)) {
            const DWORD e = GetLastError();
            DeleteFileW(LongPath(temp).c_str());
            detail = "cannot replace the file (error " + std::to_string(e) + ")";
            return false;
        }
        return true;
    }

    // Whether the file is still what the restore saw when it was decided.
    bool StillAsSeen(const RestoreOp& op, bool verifyContent) const {
        const Probe now = ProbePath(FullPath(op.path));
        if (!op.fromExists) return now.kind == Probe::Absent;
        if (now.kind != Probe::File || now.size != op.fromSize || now.mtime != op.fromMtime) return false;
        if (!verifyContent || op.fromHash.empty()) return true;
        std::string content, hash;
        return ReadWhole(FullPath(op.path), options.maxFileBytes, content) == ReadStatus::Ok && Sha256Hex(content, hash) &&
               hash == op.fromHash;
    }
    bool AlreadyAtTarget(const RestoreOp& op) const {
        const Probe now = ProbePath(FullPath(op.path));
        if (op.target.kind == State::Absent) return now.kind == Probe::Absent;
        if (now.kind != Probe::File || now.size != op.target.size) return false;
        std::string content, hash;
        return ReadWhole(FullPath(op.path), options.maxFileBytes, content) == ReadStatus::Ok && Sha256Hex(content, hash) &&
               hash == op.target.hash;
    }

    void RemoveCreatedDirs(const std::vector<std::string>& dirs) const {
        std::vector<std::string> ordered = dirs;  // deepest first; RemoveDirectoryW only removes an empty folder
        std::sort(ordered.begin(), ordered.end(), [](const std::string& a, const std::string& b) {
            const auto depth = [](const std::string& s) { return std::count(s.begin(), s.end(), '/'); };
            return depth(a) != depth(b) ? depth(a) > depth(b) : a > b;
        });
        for (const auto& d : ordered) {
            if (!ParentsAreSafe(d + "/x", nullptr)) continue;
            RemoveDirectoryW(LongPath(FullPath(d)).c_str());
        }
    }

    // Runs a journal to its end: removals, emptied folders, writes; then marks the turns undone, records the
    // folder again and drops the journal. Returns false when a fault stopped it (tests).
    bool RunJournal(const Journal& journal, bool resuming, Result& out) {
        bool dirsDone = false;
        std::set<std::string> leftAlone;
        for (const auto& c : out.conflicts) leftAlone.insert(c.path);
        const auto removeDirs = [&] {
            if (!dirsDone) RemoveCreatedDirs(journal.removeDirs);
            dirsDone = true;
        };
        for (const RestoreOp& op : journal.ops) {
            if (op.target.kind != State::Absent) removeDirs();
            if (Fault("restore.op")) {
                out.status = Status::IoError;
                out.detail = "fault injected at restore.op";
                return false;
            }
            if (resuming) {
                if (op.target.kind != State::Absent) DeleteFileW(LongPath(ParentOf(FullPath(op.path)) + L"\\" + Widen(op.temp)).c_str());
                if (AlreadyAtTarget(op)) {
                    out.restored.push_back(op.path);
                    continue;
                }
            }
            // The file must still be what was seen when the restore was decided: a save that slipped in between
            // (or since the crash) is the user's, and stays.
            if (!ParentsAreSafe(op.path, nullptr) || !StillAsSeen(op, resuming)) {
                if (!leftAlone.count(op.path)) out.conflicts.push_back({op.path, ConflictReason::ModifiedSinceTurn});
                out.skipped.push_back(op.path);
                continue;
            }
            std::string detail;
            if (ApplyOp(op, detail)) {
                out.restored.push_back(op.path);
            } else if (detail == "fault") {
                out.status = Status::IoError;
                out.detail = "fault injected at restore.temp";
                return false;
            } else {
                out.failed.push_back(op.path);
                if (out.detail.empty()) out.detail = op.path + ": " + detail;
            }
        }
        removeDirs();
        if (Fault("restore.applied")) {
            out.status = Status::IoError;
            out.detail = "fault injected at restore.applied";
            return false;
        }
        std::vector<TurnRecord> turns;
        LoadTurns(turns);
        for (auto& t : turns) {
            if (std::find(journal.turns.begin(), journal.turns.end(), t.id) == journal.turns.end()) continue;
            if (!t.undone) {
                t.undone = true;
                SaveTurn(t);
            }
            out.undoneTurns.push_back(t.id);
        }
        if (Fault("restore.records")) {
            out.status = Status::IoError;
            out.detail = "fault injected at restore.records";
            return false;
        }
        Index before, after;
        const bool had = LoadIndex(before);
        if (Scan(had ? &before : nullptr, after, nullptr).status == Status::Ok) SaveIndex(after);
        DeleteFileW(LongPath(JournalPath()).c_str());
        if (!out.failed.empty()) out.status = Status::Partial;
        return true;
    }

    // The bound folder may have been deleted or moved since: then nothing is written (we would re-create it).
    bool ProjectStillThere() const {
        const DWORD attrs = GetFileAttributesW(LongPath(projectDir).c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    void ResumeRestore(Result& out) {
        if (!ProjectStillThere()) return;  // the journal waits for the folder to come back
        std::string text;
        Journal journal;
        if (ReadWhole(JournalPath(), 256ull << 20, text) != ReadStatus::Ok || !ParseJournal(text, journal)) {
            DeleteFileW(LongPath(JournalPath()).c_str());  // unreadable: nothing we can safely redo
            return;
        }
        out.resumedRestore = true;
        RunJournal(journal, true, out);
    }

    // Undoes `undo` (oldest first, none of them undone). Caller holds the lock and has run Recover.
    Result RestoreTurns(const std::vector<TurnRecord>& undo, OnConflict onConflict) {
        Result out;
        if (undo.empty()) return out;
        if (!ProjectStillThere()) return Fail(Status::InvalidProject, "the project folder is gone; nothing was touched");
        struct Item {
            State target, expected;
            bool between = false;
        };
        std::map<std::string, Item> plan;
        std::set<std::string> createdDirs;
        for (const TurnRecord& t : undo) {
            for (const ChangeRecord& c : t.changes) {
                const auto it = plan.find(c.path);
                if (it == plan.end()) {
                    plan.emplace(c.path, Item{c.before, c.after, false});
                } else {
                    if (!SameState(it->second.expected, c.before)) it->second.between = true;
                    it->second.expected = c.after;
                }
            }
            createdDirs.insert(t.createdDirs.begin(), t.createdDirs.end());
        }
        const auto willRemove = [&](std::string_view path) {
            const auto it = plan.find(std::string(path));
            return it != plan.end() && it->second.target.kind == State::Absent;
        };

        Journal journal;
        journal.id = RandomSuffix().substr(0, 8);
        for (const auto& t : undo) journal.turns.push_back(t.id);
        journal.removeDirs.assign(createdDirs.begin(), createdDirs.end());
        std::vector<RestoreOp> removals, writes;
        std::vector<Conflict> conflicts;
        std::size_t counter = 0;
        const std::wstring blobs = BlobsDir();
        for (const auto& planned : plan) {
            const std::string& path = planned.first;
            const Item& item = planned.second;
            if (item.target.kind == State::Opaque) {
                out.skipped.push_back(path);  // its content before the turn was never kept
                continue;
            }
            const std::wstring full = FullPath(path);
            const Probe now = ProbePath(full);
            RestoreOp op;
            op.path = path;
            op.target = item.target;
            op.fromExists = now.kind == Probe::File;
            op.fromSize = now.size;
            op.fromMtime = now.mtime;
            op.temp = "~" + journal.id + "-" + std::to_string(counter++) + ".snaptmp";

            bool conflict = false;
            const auto flag = [&](ConflictReason reason) {
                if (!conflict) conflicts.push_back({path, reason});
                conflict = true;
            };
            bool hard = false;  // nothing we could do even when asked to overwrite
            if (!ParentsAreSafe(path, willRemove)) {
                flag(ConflictReason::Blocked);
                hard = true;
            } else if (now.kind == Probe::Dir) {
                if (item.target.kind == State::Absent || !createdDirs.count(path)) {
                    flag(ConflictReason::Blocked);
                    hard = true;
                }
            }
            std::string currentHash;
            bool currentRead = false;
            const auto readCurrent = [&] {
                if (currentRead) return;
                currentRead = true;
                std::string content;
                if (now.kind == Probe::File && now.size <= options.maxFileBytes &&
                    ReadWhole(full, options.maxFileBytes, content) == ReadStatus::Ok)
                    Sha256Hex(content, currentHash);
            };
            if (!hard) {
                // Already as wanted (the user put it back, or nothing to do): leave it, no conflict.
                bool atTarget = false;
                if (item.target.kind == State::Absent) {
                    atTarget = now.kind == Probe::Absent;
                } else if (now.kind == Probe::File && now.size == item.target.size) {
                    readCurrent();
                    atTarget = currentHash == item.target.hash;
                }
                if (atTarget) continue;
                // Is it as the last undone turn left it? (A folder still standing here is one the undone turns
                // created: it is removed before the file is written, so it counts as "nothing there".)
                if (item.expected.kind == State::Absent) {
                    if (now.kind == Probe::File) flag(ConflictReason::CreatedSinceTurn);
                } else if (now.kind != Probe::File) {
                    flag(ConflictReason::DeletedSinceTurn);
                } else if (item.expected.kind == State::Content) {
                    if (now.size != item.expected.size) {
                        flag(ConflictReason::ModifiedSinceTurn);
                    } else {
                        readCurrent();
                        if (currentHash != item.expected.hash) flag(ConflictReason::ModifiedSinceTurn);
                    }
                } else if (now.size != item.expected.size || now.mtime != item.expected.mtime) {
                    flag(ConflictReason::ModifiedSinceTurn);
                }
                if (item.between) flag(ConflictReason::EditedBetweenTurns);
                if (now.kind == Probe::File) {
                    bool usable = !(now.attrs & FILE_ATTRIBUTE_READONLY);
                    if (usable) {
                        Handle probe(CreateFileW(LongPath(full).c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                 nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
                        usable = probe.Valid();
                    }
                    if (!usable) flag(ConflictReason::InUse);
                }
                if (item.target.kind == State::Content) {
                    std::string content;
                    if (!ReadBlob(blobs, item.target.hash, content)) {
                        if (conflict) conflicts.back().reason = ConflictReason::SnapshotLost;
                        else flag(ConflictReason::SnapshotLost);
                        hard = true;
                    }
                }
            }
            if (currentRead) op.fromHash = currentHash;
            if (conflict && (hard || onConflict != OnConflict::Overwrite)) {
                if (onConflict != OnConflict::Refuse) out.skipped.push_back(path);
                continue;
            }
            if (conflict) conflicts.pop_back();  // Overwrite: the user chose; it is no longer left alone
            (op.target.kind == State::Absent ? removals : writes).push_back(std::move(op));
        }
        if (onConflict == OnConflict::Refuse && !conflicts.empty()) {
            Result refused = Fail(Status::Conflict, "files changed since the turn; nothing was touched");
            refused.conflicts = std::move(conflicts);
            return refused;
        }
        out.conflicts = std::move(conflicts);
        journal.ops = std::move(removals);
        journal.ops.insert(journal.ops.end(), std::make_move_iterator(writes.begin()), std::make_move_iterator(writes.end()));

        // The journal first: from here on a crash is finished by the next Open / BeginTurn.
        if (!WriteAtomic(JournalPath(), JournalJson(journal))) return Fail(Status::IoError, "cannot write the restore journal");
        if (Fault("restore.journal")) return Fail(Status::IoError, "fault injected at restore.journal");
        RunJournal(journal, false, out);
        return out;
    }

    // ------------------------------------------------------------ purge
    struct BlobFile {
        std::wstring path;
        std::uint64_t size;
    };
    void ListBlobs(std::unordered_map<std::string, BlobFile>& blobs, std::vector<std::wstring>& strays) const {
        WIN32_FIND_DATAW fd;
        const std::wstring root = BlobsDir();
        const HANDLE h = FindFirstFileExW(LongPath(root + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                          nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            const std::wstring bucket = fd.cFileName;
            if (bucket == L"." || bucket == L".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                continue;
            WIN32_FIND_DATAW inner;
            const HANDLE hi = FindFirstFileExW(LongPath(root + L"\\" + bucket + L"\\*").c_str(), FindExInfoBasic, &inner,
                                               FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (hi == INVALID_HANDLE_VALUE) continue;
            do {
                if (inner.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::wstring full = root + L"\\" + bucket + L"\\" + inner.cFileName;
                const std::string hash = Narrow(bucket) + Narrow(inner.cFileName);
                if (IsHash(hash)) blobs[hash] = {full, (std::uint64_t(inner.nFileSizeHigh) << 32) | inner.nFileSizeLow};
                else strays.push_back(full);  // a temporary file left by a crash
            } while (FindNextFileW(hi, &inner));
            FindClose(hi);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // Applies the limits. Caller holds the lock. Nothing happens while a restore is pending.
    void PurgeLocked(Usage* usage) {
        if (usage) *usage = Usage();
        if (FileExists(JournalPath())) return;
        std::vector<TurnRecord> turns;
        LoadTurns(turns);
        const std::size_t keep = std::max<std::size_t>(options.maxTurns, 1);
        const auto drop = [&](const TurnRecord& t) { DeleteFileW(LongPath(TurnPath(t)).c_str()); };

        // 1. Count: the `keep` latest turns that changed something, and everything newer than the oldest of them.
        std::size_t withChanges = 0, firstKept = 0;
        for (std::size_t i = turns.size(); i-- > 0;) {
            if (turns[i].changes.empty()) continue;
            if (++withChanges > keep) {
                firstKept = i + 1;
                break;
            }
        }
        if (turns.size() - firstKept > keep * 8) firstKept = turns.size() - keep * 8;  // empty turns do not pile up
        for (std::size_t i = 0; i < firstKept; ++i) drop(turns[i]);
        turns.erase(turns.begin(), turns.begin() + std::ptrdiff_t(firstKept));

        // 2. Contents: what the known state and the remaining turns refer to stays; the rest goes.
        std::unordered_map<std::string, int> refs;
        Index index;
        if (LoadIndex(index))
            for (const auto& e : index.files)
                if (!e.hash.empty()) ++refs[e.hash];
        const auto count = [&](const TurnRecord& t, int delta) {
            for (const auto& c : t.changes) {
                if (c.before.kind == State::Content) refs[c.before.hash] += delta;
                if (c.after.kind == State::Content) refs[c.after.hash] += delta;
            }
        };
        for (const auto& t : turns) count(t, 1);
        std::unordered_map<std::string, BlobFile> blobs;
        std::vector<std::wstring> strays;
        ListBlobs(blobs, strays);
        for (const auto& s : strays) DeleteFileW(LongPath(s).c_str());
        std::uint64_t total = 0;
        for (auto it = blobs.begin(); it != blobs.end();) {
            const auto ref = refs.find(it->first);
            if (ref == refs.end() || ref->second <= 0) {
                DeleteFileW(LongPath(it->second.path).c_str());
                it = blobs.erase(it);
            } else {
                total += it->second.size;
                ++it;
            }
        }

        // 3. Size: oldest turns go until the store fits; the latest turn with changes always stays.
        std::size_t lastChanged = turns.size();
        for (std::size_t i = turns.size(); i-- > 0;)
            if (!turns[i].changes.empty()) {
                lastChanged = i;
                break;
            }
        std::size_t dropped = 0;
        while (total > options.maxStoreBytes && dropped < turns.size() && dropped < lastChanged) {
            const TurnRecord& t = turns[dropped];
            count(t, -1);
            for (const auto& c : t.changes) {
                for (const State* s : {&c.before, &c.after}) {
                    if (s->kind != State::Content) continue;
                    const auto ref = refs.find(s->hash);
                    const auto blob = blobs.find(s->hash);
                    if (ref != refs.end() && ref->second <= 0 && blob != blobs.end()) {
                        DeleteFileW(LongPath(blob->second.path).c_str());
                        total -= blob->second.size;
                        blobs.erase(blob);
                    }
                }
            }
            drop(t);
            ++dropped;
        }
        if (usage) {
            usage->turns = turns.size() - dropped;
            usage->blobs = blobs.size();
            usage->blobBytes = total;
        }
    }
};

Store::Store(std::wstring storeRoot, Options options) : impl_(new Impl) {
    impl_->storeRootGiven = std::move(storeRoot);
    impl_->options = std::move(options);
}
Store::~Store() = default;

bool Store::HasProject() const {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return !impl_->projectDir.empty();
}
bool Store::TurnIsOpen() const {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->turnOpen;
}
const std::wstring& Store::ProjectDir() const { return impl_->projectDir; }
std::wstring Store::ProjectStoreDir() const {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->projectStore;
}
const Options& Store::GetOptions() const { return impl_->options; }
void Store::SetLimits(std::size_t maxTurns, std::uint64_t maxStoreBytes) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    impl_->options.maxTurns = maxTurns;
    impl_->options.maxStoreBytes = maxStoreBytes;
}

Result Store::Open(const std::wstring& projectDir) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Result bound = impl_->Bind(projectDir);
    if (!bound) return bound;
    Handle lock;
    Result locked = impl_->Lock(lock);
    if (!locked) return locked;
    Result out;
    impl_->Recover(out, nullptr);
    return out;
}

Result Store::BeginTurn(const std::wstring& projectDir, std::string* turnId, ScanStats* stats) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Impl& s = *impl_;
    if (turnId) turnId->clear();
    if (s.turnOpen) return Impl::Fail(Status::TurnOpen, "a turn is already running");
    Result bound = s.Bind(projectDir);
    if (!bound) return bound;
    Handle lock;
    Result locked = s.Lock(lock);
    if (!locked) return locked;
    Result out;
    bool elsewhere = false;
    s.Recover(out, &elsewhere);
    if (out.status == Status::IoError) return out;  // a fault stopped the recovery (tests)
    if (elsewhere) return Impl::Fail(Status::TurnOpen, "a turn is running on this project in another window");

    Index before, now;
    const bool had = s.LoadIndex(before);
    const ScanOutcome scan = s.Scan(had ? &before : nullptr, now, stats);
    if (scan.status != Status::Ok) {
        Result failed = Impl::Fail(scan.status, scan.detail);
        failed.resumedRestore = out.resumedRestore;
        return failed;
    }
    Handle turnLock;
    if (!AcquireLock(s.TurnLockPath(), TurnLockWaitMs, turnLock))
        return Impl::Fail(Status::TurnOpen, "a turn is running on this project");
    if ((!had || !SameIndex(before, now)) && !s.SaveIndex(now)) return Impl::Fail(Status::IoError, "cannot write the index");

    std::vector<TurnRecord> turns;
    std::uint64_t highest = 0;
    s.LoadTurns(turns, nullptr, &highest);
    TurnRecord turn;
    const std::string suffix = RandomSuffix();
    turn.id = "trn_" + suffix;
    turn.checkpoint = "chk_" + suffix;
    turn.seq = highest + 1;
    turn.startedAt = s.Now();
    if (!s.SaveMarker(turn)) return Impl::Fail(Status::IoError, "cannot write the open-turn marker");
    s.openTurn = turn;
    s.turnLock.Take(turnLock);
    s.turnOpen = true;
    if (turnId) *turnId = turn.id;
    out.status = Status::Ok;
    return out;
}

Result Store::EndTurn(TurnSummary* summary, ScanStats* stats, const std::vector<std::string>* onlyFiles) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Impl& s = *impl_;
    if (summary) *summary = TurnSummary();
    if (!s.turnOpen) return Impl::Fail(Status::NoTurnOpen, "no turn is running");
    Handle lock;
    Result locked = s.Lock(lock);
    if (!locked) return locked;  // still open: the caller may try again
    Result out = s.FinishTurn(s.openTurn, false, summary, stats, onlyFiles);
    s.turnLock.Close();
    s.turnOpen = false;
    if (out.status != Status::IoError) s.PurgeLocked(nullptr);
    return out;
}

Result Store::UndoTurn(const std::string& turnId, OnConflict onConflict) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Impl& s = *impl_;
    if (s.turnOpen) return Impl::Fail(Status::TurnOpen, "a turn is running");
    Handle lock;
    Result locked = s.Lock(lock);
    if (!locked) return locked;
    Result recovered;
    bool elsewhere = false;
    s.Recover(recovered, &elsewhere);
    if (recovered.status == Status::IoError) return recovered;
    if (elsewhere) return Impl::Fail(Status::TurnOpen, "a turn is running on this project in another window");
    std::vector<TurnRecord> turns;
    std::vector<std::uint64_t> damaged;
    s.LoadTurns(turns, &damaged);
    const auto it = std::find_if(turns.begin(), turns.end(), [&](const TurnRecord& t) { return t.id == turnId; });
    if (it == turns.end()) return Impl::Fail(Status::NotFound, "unknown turn");
    if (it->undone) return Impl::Fail(Status::AlreadyUndone, "this turn was already undone");
    for (const std::uint64_t seq : damaged)
        if (seq >= it->seq) return Impl::Fail(Status::Corrupt, "a later turn's record is unreadable; nothing was touched");
    std::vector<TurnRecord> undo;
    for (auto t = it; t != turns.end(); ++t)
        if (!t->undone) undo.push_back(*t);
    Result out = s.RestoreTurns(undo, onConflict);
    out.resumedRestore = recovered.resumedRestore;
    return out;
}

Result Store::Restore(const std::string& checkpointId, OnConflict onConflict) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Impl& s = *impl_;
    if (s.turnOpen) return Impl::Fail(Status::TurnOpen, "a turn is running");
    Handle lock;
    Result locked = s.Lock(lock);
    if (!locked) return locked;
    Result recovered;
    bool elsewhere = false;
    s.Recover(recovered, &elsewhere);
    if (recovered.status == Status::IoError) return recovered;
    if (elsewhere) return Impl::Fail(Status::TurnOpen, "a turn is running on this project in another window");
    std::vector<TurnRecord> turns;
    std::vector<std::uint64_t> damaged;
    s.LoadTurns(turns, &damaged);
    const auto it = std::find_if(turns.begin(), turns.end(), [&](const TurnRecord& t) { return t.checkpoint == checkpointId; });
    if (it == turns.end()) return Impl::Fail(Status::NotFound, "unknown checkpoint");
    if (it->undone) return Impl::Fail(Status::AlreadyUndone, "this checkpoint's turn was undone");
    for (const std::uint64_t seq : damaged)
        if (seq > it->seq) return Impl::Fail(Status::Corrupt, "a later turn's record is unreadable; nothing was touched");
    std::vector<TurnRecord> undo;
    for (auto t = it + 1; t != turns.end(); ++t)
        if (!t->undone) undo.push_back(*t);
    Result out = s.RestoreTurns(undo, onConflict);
    out.resumedRestore = recovered.resumedRestore;
    return out;
}

Result Store::Diff(const std::string& file, const std::string& checkpointId, FileDiff* diff, std::size_t maxBytes) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Impl& s = *impl_;
    if (diff) *diff = FileDiff();
    Handle lock;
    Result locked = s.Lock(lock);
    if (!locked) return locked;
    std::vector<TurnRecord> turns;
    s.LoadTurns(turns);
    const auto turn = std::find_if(turns.begin(), turns.end(), [&](const TurnRecord& t) { return t.checkpoint == checkpointId; });
    if (turn == turns.end()) return Impl::Fail(Status::NotFound, "unknown checkpoint");
    const auto change = std::find_if(turn->changes.begin(), turn->changes.end(), [&](const ChangeRecord& c) { return c.path == file; });
    if (change == turn->changes.end()) return Impl::Fail(Status::NotFound, "this turn did not change that file");
    FileDiff result;
    result.path = change->path;
    result.kind = change->Kind();
    const bool kept = change->before.kind != State::Opaque && change->after.kind != State::Opaque;
    Result out;
    if (kept) {
        std::string before, after;
        const bool needBefore = change->before.kind == State::Content, needAfter = change->after.kind == State::Content;
        if ((needBefore && !ReadBlob(s.BlobsDir(), change->before.hash, before)) ||
            (needAfter && !ReadBlob(s.BlobsDir(), change->after.hash, after))) {
            out = Impl::Fail(Status::Corrupt, "the kept content is missing or damaged");
        } else {
            result = DiffContents(change->path, needBefore ? &before : nullptr, needAfter ? &after : nullptr, true, maxBytes);
        }
    }
    result.kind = change->Kind();
    result.restorable = change->before.kind != State::Opaque;
    result.notKept = change->before.kind == State::Opaque ? change->before.notKept
                                                           : (change->after.kind == State::Opaque ? change->after.notKept : NotKept::No);
    if (diff) *diff = std::move(result);
    return out;
}

Result Store::Turns(std::vector<TurnSummary>* turns) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Impl& s = *impl_;
    if (turns) turns->clear();
    Handle lock;
    Result locked = s.Lock(lock);
    if (!locked) return locked;
    std::vector<TurnRecord> records;
    std::vector<std::uint64_t> damaged;
    s.LoadTurns(records, &damaged);
    if (turns)
        for (const auto& t : records) turns->push_back(Summarize(t));
    Result out;
    if (!damaged.empty()) out.detail = std::to_string(damaged.size()) + " unreadable turn record(s) skipped";
    return out;
}

Result Store::Purge(Usage* after) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Impl& s = *impl_;
    if (after) *after = Usage();
    Handle lock;
    Result locked = s.Lock(lock);
    if (!locked) return locked;
    s.PurgeLocked(after);
    return Result();
}

Result Store::GetUsage(Usage* usage) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Impl& s = *impl_;
    if (usage) *usage = Usage();
    Handle lock;
    Result locked = s.Lock(lock);
    if (!locked) return locked;
    std::vector<TurnRecord> turns;
    s.LoadTurns(turns);
    std::unordered_map<std::string, Impl::BlobFile> blobs;
    std::vector<std::wstring> strays;
    s.ListBlobs(blobs, strays);
    if (usage) {
        usage->turns = turns.size();
        usage->blobs = blobs.size();
        for (const auto& b : blobs) usage->blobBytes += b.second.size;
    }
    return Result();
}

Result Store::Forget() {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    Impl& s = *impl_;
    if (s.turnOpen) return Impl::Fail(Status::TurnOpen, "a turn is running");
    Handle lock;
    Result locked = s.Lock(lock);
    if (!locked) return locked;
    if (FileExists(s.MarkerPath())) {
        Handle stale;
        if (!AcquireLock(s.TurnLockPath(), TurnLockWaitMs, stale))
            return Impl::Fail(Status::TurnOpen, "a turn is running on this project in another window");
    }
    DeleteTree(s.projectStore, L"lock", false);
    s.WriteProjectFile(s.projectStore, s.projectDir);
    return Result();
}

}  // namespace ChangeSnapshots
