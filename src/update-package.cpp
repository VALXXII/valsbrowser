#include "update-package.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace UpdatePackage {
namespace {

const char Magic[8] = {'L', 'S', 'R', 'U', 'P', 'D', '0', '1'};
const char Trailer[8] = {'L', 'S', 'R', 'U', 'E', 'N', 'D', '1'};
constexpr std::size_t ChunkBytes = 256 * 1024;

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE v) : h(v) {}
    ~Handle() { Close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void Close() {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
    bool Valid() const { return h != INVALID_HANDLE_VALUE && h != nullptr; }
};

// Prefix \\?\ for absolute drive paths so long staged paths keep working. Inputs are already normalized.
std::wstring Long(const std::wstring& p) {
    if (p.size() < 200 || p.rfind(L"\\\\", 0) == 0) return p;
    if (p.size() > 2 && p[1] == L':' && p[2] == L'\\') return L"\\\\?\\" + p;
    return p;
}

std::string Lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
std::wstring LowerW(std::wstring s) {
    CharLowerBuffW(s.data(), DWORD(s.size()));
    return s;
}

bool ParseU32Field(const std::string& s, std::uint32_t& out) {
    if (s.empty() || s.size() > 9) return false;
    if (s.size() > 1 && s[0] == '0') return false;
    std::uint32_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + std::uint32_t(c - '0');
    }
    out = v;
    return true;
}

bool ParseU64(const std::string& s, std::uint64_t& out) {
    if (s.empty() || s.size() > 19 || (s.size() > 1 && s[0] == '0')) return false;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + std::uint64_t(c - '0');
    }
    out = v;
    return true;
}

bool ReadExact(HANDLE h, void* buffer, std::size_t size) {
    auto* p = static_cast<std::uint8_t*>(buffer);
    while (size) {
        DWORD want = DWORD(std::min<std::size_t>(size, ChunkBytes)), got = 0;
        if (!ReadFile(h, p, want, &got, nullptr) || got == 0) return false;
        p += got;
        size -= got;
    }
    return true;
}
bool WriteExact(HANDLE h, const void* buffer, std::size_t size) {
    auto* p = static_cast<const std::uint8_t*>(buffer);
    while (size) {
        DWORD want = DWORD(std::min<std::size_t>(size, ChunkBytes)), put = 0;
        if (!WriteFile(h, p, want, &put, nullptr) || put == 0) return false;
        p += put;
        size -= put;
    }
    return true;
}
bool Seek(HANDLE h, std::uint64_t pos) {
    LARGE_INTEGER li;
    li.QuadPart = LONGLONG(pos);
    return SetFilePointerEx(h, li, nullptr, FILE_BEGIN) != 0;
}
bool FileSize(HANDLE h, std::uint64_t& size) {
    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li) || li.QuadPart < 0) return false;
    size = std::uint64_t(li.QuadPart);
    return true;
}

std::wstring StripFinalPrefix(std::wstring p) {
    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0) return L"\\\\" + p.substr(8);
    if (p.rfind(L"\\\\?\\", 0) == 0) return p.substr(4);
    return p;
}
std::wstring FinalPathOf(HANDLE h) {
    wchar_t buffer[4096];
    DWORD n = GetFinalPathNameByHandleW(h, buffer, 4096, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= 4096) return L"";
    return StripFinalPrefix(std::wstring(buffer, n));
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        auto pos = s.find(sep, start);
        parts.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return parts;
}

std::wstring Join(const std::wstring& root, const std::string& rel) {
    std::wstring out = root;
    if (!out.empty() && out.back() != L'\\') out += L'\\';
    for (char c : rel) out += c == '/' ? L'\\' : wchar_t(static_cast<unsigned char>(c));
    return out;
}

struct StageList {
    std::string version;
    std::map<std::string, std::pair<std::string, std::pair<std::uint64_t, Digest>>> files;  // lower -> (path,(size,sha))
};

std::string FormatStage(const std::string& version, const std::vector<Entry>& entries) {
    std::string s = "browser-stage 1\nversion " + version + "\n";
    for (const auto& e : entries) s += "file " + Hex(e.sha256) + " " + std::to_string(e.size) + " " + e.path + "\n";
    return s;
}

bool ParseStage(const std::string& text, StageList& out, std::string& error) {
    if (text.empty() || text.back() != '\n') return error = "stage list truncated", false;
    auto lines = Split(text.substr(0, text.size() - 1), '\n');
    if (lines.size() < 3 || lines[0] != "browser-stage 1" || lines[1].rfind("version ", 0) != 0)
        return error = "stage list header invalid", false;
    out.version = lines[1].substr(8);
    Version v;
    if (!ParseVersion(out.version, v)) return error = "stage version invalid", false;
    std::set<std::string> dirs;
    for (std::size_t i = 2; i < lines.size(); ++i) {
        const auto& l = lines[i];
        if (l.size() < 5 + 64 + 3 || l.compare(0, 5, "file ") != 0 || l[69] != ' ')
            return error = "stage entry invalid", false;
        Digest d;
        if (!ParseHex(l.substr(5, 64), d)) return error = "stage hash invalid", false;
        auto space = l.find(' ', 70);
        if (space == std::string::npos) return error = "stage entry invalid", false;
        std::uint64_t size;
        if (!ParseU64(l.substr(70, space - 70), size) || size > MaxEntryBytes) return error = "stage size invalid", false;
        std::string path = l.substr(space + 1);
        if (!ValidEntryPath(path, error) || !AllowedPayloadPath(path)) return error = "stage path rejected: " + path, false;
        auto key = Lower(path);
        if (!out.files.emplace(key, std::make_pair(path, std::make_pair(size, d))).second)
            return error = "stage duplicate path", false;
    }
    if (out.files.size() > MaxEntries) return error = "stage too many entries", false;
    return true;
}

}  // namespace

// ---------------------------------------------------------------- versions / manifest

bool ParseVersion(const std::string& text, Version& out) {
    auto parts = Split(text, '.');
    if (parts.size() != 3) return false;
    Version v;
    if (!ParseU32Field(parts[0], v.major) || !ParseU32Field(parts[1], v.minor) || !ParseU32Field(parts[2], v.build))
        return false;
    out = v;
    return true;
}
bool ParseVersion(const std::wstring& text, Version& out) {
    for (wchar_t c : text)
        if (c > 127) return false;
    return ParseVersion(Narrow(text), out);
}
int CompareVersion(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.build != b.build) return a.build < b.build ? -1 : 1;
    return 0;
}
bool IsZero(const Version& v) { return v.major == 0 && v.minor == 0 && v.build == 0; }

std::string PackageFileName(const std::string& version) { return "browser-" + version + "-win-x64.lsrupd"; }

bool ParseManifest(const std::string& text, Manifest& out, std::string& error) {
    if (text.empty() || text.size() > MaxManifestBytes) return error = "manifest size invalid", false;
    for (unsigned char c : text)
        if (c != '\n' && (c < 0x20 || c > 0x7e)) return error = "manifest contains invalid bytes", false;
    std::string body = text;
    if (body.back() == '\n') body.pop_back();
    auto lines = Split(body, '\n');
    static const char* keys[] = {"browser-update", "version", "arch", "package", "size", "sha256"};
    if (lines.size() != 6) return error = "manifest must have exactly 6 lines", false;
    std::string values[6];
    for (int i = 0; i < 6; ++i) {
        std::string prefix = std::string(keys[i]) + " ";
        if (lines[i].compare(0, prefix.size(), prefix) != 0 || lines[i].size() == prefix.size())
            return error = std::string("manifest line invalid: ") + keys[i], false;
        values[i] = lines[i].substr(prefix.size());
        if (values[i].find(' ') != std::string::npos) return error = std::string("manifest value invalid: ") + keys[i], false;
    }
    if (values[0] != "1") return error = "manifest format unsupported", false;
    Manifest m;
    Version v;
    if (!ParseVersion(values[1], v) || IsZero(v)) return error = "manifest version invalid", false;
    m.version = values[1];
    if (values[2] != Arch) return error = "manifest arch mismatch", false;
    m.arch = values[2];
    if (values[3] != PackageFileName(m.version)) return error = "manifest package name mismatch", false;
    m.package = values[3];
    if (!ParseU64(values[4], m.size) || m.size == 0 || m.size > MaxPackageBytes) return error = "manifest size invalid", false;
    if (values[5].size() != 64 || !ParseHex(values[5], m.sha256) || Hex(m.sha256) != values[5])
        return error = "manifest sha256 invalid", false;
    out = m;
    return true;
}

std::string FormatManifest(const Manifest& m) {
    return "browser-update 1\nversion " + m.version + "\narch " + m.arch + "\npackage " + m.package + "\nsize " +
           std::to_string(m.size) + "\nsha256 " + Hex(m.sha256) + "\n";
}

bool ValidRepository(const std::wstring& repo) {
    auto slash = repo.find(L'/');
    if (slash == std::wstring::npos || repo.find(L'/', slash + 1) != std::wstring::npos) return false;
    auto part = [](const std::wstring& p) {
        if (p.empty() || p.size() > 100 || p == L"." || p == L"..") return false;
        for (wchar_t c : p)
            if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'.' ||
                  c == L'_' || c == L'-'))
                return false;
        return true;
    };
    return part(repo.substr(0, slash)) && part(repo.substr(slash + 1));
}

// ---------------------------------------------------------------- SHA-256

Sha256::Sha256() {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return;
    alg_ = alg;
    DWORD objectLength = 0, got = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &got, 0) != 0)
        return;
    object_.resize(objectLength);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(alg, &hash, object_.data(), objectLength, nullptr, 0, 0) != 0) return;
    hash_ = hash;
}
Sha256::~Sha256() {
    if (hash_) BCryptDestroyHash(hash_);
    if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
}
void Sha256::Update(const void* data, std::size_t size) {
    auto* p = static_cast<const std::uint8_t*>(data);
    while (hash_ && size) {
        ULONG n = ULONG(std::min<std::size_t>(size, 1u << 30));
        BCryptHashData(hash_, const_cast<PUCHAR>(p), n, 0);
        p += n;
        size -= n;
    }
}
Digest Sha256::Final() {
    Digest d{};
    if (hash_) {
        BCryptFinishHash(hash_, d.data(), ULONG(d.size()), 0);
        BCryptDestroyHash(hash_);
        hash_ = nullptr;
    }
    return d;
}
Digest Sha256Of(const std::string& data) {
    Sha256 h;
    h.Update(data.data(), data.size());
    return h.Final();
}
bool Sha256File(const std::wstring& path, Digest& out, std::uint64_t& size) {
    Handle f(CreateFileW(Long(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!f.Valid()) return false;
    Sha256 h;
    if (!h.Ok()) return false;
    std::vector<std::uint8_t> buffer(ChunkBytes);
    size = 0;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f.h, buffer.data(), DWORD(buffer.size()), &got, nullptr)) return false;
        if (!got) break;
        h.Update(buffer.data(), got);
        size += got;
    }
    out = h.Final();
    return true;
}
std::string Hex(const Digest& d) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (auto b : d) {
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}
bool ParseHex(const std::string& hex, Digest& out) {
    if (hex.size() != 64) return false;
    for (std::size_t i = 0; i < 32; ++i) {
        int v = 0;
        for (int k = 0; k < 2; ++k) {
            char c = hex[i * 2 + k];
            int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (n < 0) return false;
            v = v * 16 + n;
        }
        out[i] = std::uint8_t(v);
    }
    return true;
}

// ---------------------------------------------------------------- path policy

bool ValidEntryPath(const std::string& path, std::string& error) {
    if (path.empty() || path.size() > MaxPathBytes) return error = "path length invalid", false;
    for (char c : path) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                  c == '-' || c == ' ' || c == '(' || c == ')' || c == '+' || c == '/';
        if (!ok) return error = "path contains forbidden character", false;
    }
    if (path.front() == '/' || path.back() == '/') return error = "absolute or directory path", false;
    static const char* devices[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                                    "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
                                    "conin$", "conout$"};
    for (const auto& part : Split(path, '/')) {
        if (part.empty()) return error = "empty path component", false;
        if (part == "." || part == "..") return error = "path traversal", false;
        if (part.front() == '.' || part.front() == ' ') return error = "hidden or padded component", false;
        if (part.back() == '.' || part.back() == ' ') return error = "trailing dot or space", false;
        auto stem = Lower(part.substr(0, part.find('.')));
        for (auto d : devices)
            if (stem == d) return error = "reserved device name", false;
    }
    return true;
}

bool AllowedPayloadPath(const std::string& path) {
    if (path.find('/') == std::string::npos) {
        static const char* top[] = {"browser.exe", "update-helper.exe", "WebView2Loader.dll", "build-info.txt", "version.json"};
        for (auto t : top)
            if (path == t) return true;
        auto ends = [&](const char* suffix) {
            std::size_t n = std::strlen(suffix);
            return path.size() > n && path.compare(path.size() - n, n, suffix) == 0;
        };
        return ends("-LICENSE.txt") || ends("-NOTICE.txt");
    }
    return path.rfind("assets/", 0) == 0;
}

// ---------------------------------------------------------------- package

bool ReadPackageIndex(const std::wstring& file, PackageIndex& out, std::string& error) {
    Handle f(CreateFileW(Long(file).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!f.Valid()) return error = "package open failed", false;
    std::uint64_t fileSize = 0;
    if (!FileSize(f.h, fileSize) || fileSize > MaxPackageBytes || fileSize < 8 + 2 + 2 + 4 + 8)
        return error = "package size invalid", false;
    std::uint64_t pos = 0;
    auto read = [&](void* p, std::size_t n) {
        if (pos + n > fileSize || !ReadExact(f.h, p, n)) return false;
        pos += n;
        return true;
    };
    char magic[8];
    if (!read(magic, 8) || std::memcmp(magic, Magic, 8) != 0) return error = "package magic invalid", false;
    auto readString = [&](std::string& s, std::size_t max) {
        std::uint16_t n = 0;
        if (!read(&n, 2) || n == 0 || n > max) return false;
        s.assign(n, '\0');
        return read(s.data(), n);
    };
    PackageIndex index;
    Version v;
    if (!readString(index.version, 32) || !ParseVersion(index.version, v)) return error = "package version invalid", false;
    if (!readString(index.arch, 16) || index.arch != Arch) return error = "package arch invalid", false;
    std::uint32_t count = 0;
    if (!read(&count, 4) || count == 0 || count > MaxEntries) return error = "package entry count invalid", false;
    std::set<std::string> files, dirs;
    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        Entry e;
        if (!readString(e.path, MaxPathBytes)) return error = "package entry path truncated", false;
        if (!ValidEntryPath(e.path, error)) return error = "package path rejected (" + error + "): " + e.path, false;
        if (!AllowedPayloadPath(e.path)) return error = "unexpected package entry: " + e.path, false;
        if (!read(&e.size, 8) || e.size > MaxEntryBytes) return error = "package entry size invalid", false;
        if (!read(e.sha256.data(), 32)) return error = "package entry hash truncated", false;
        e.offset = pos;
        if (e.size > fileSize - pos) return error = "package entry exceeds file", false;
        pos += e.size;
        if (!Seek(f.h, pos)) return error = "package seek failed", false;
        total += e.size;
        auto key = Lower(e.path);
        if (!files.insert(key).second || dirs.count(key)) return error = "duplicate package path: " + e.path, false;
        for (std::size_t s = key.find('/'); s != std::string::npos; s = key.find('/', s + 1)) {
            auto dir = key.substr(0, s);
            if (files.count(dir)) return error = "file/directory conflict: " + e.path, false;
            dirs.insert(dir);
        }
        index.entries.push_back(std::move(e));
    }
    char trailer[8];
    if (!read(trailer, 8) || std::memcmp(trailer, Trailer, 8) != 0) return error = "package trailer invalid", false;
    if (pos != fileSize) return error = "package has trailing data", false;
    if (!files.count("browser.exe") || !files.count("update-helper.exe")) return error = "package misses required files", false;
    (void)total;
    out = std::move(index);
    return true;
}

bool ExtractPackage(const std::wstring& file, const std::string& expectedVersion, const std::wstring& stagingDir,
                    const std::atomic<bool>* cancel, std::string& error) {
    PackageIndex index;
    if (!ReadPackageIndex(file, index, error)) return false;
    if (index.version != expectedVersion) return error = "package version mismatch", false;
    auto slash = stagingDir.find_last_of(L'\\');
    if (slash == std::wstring::npos) return error = "staging path invalid", false;
    std::wstring parent = stagingDir.substr(0, slash);
    if (!IsDirectory(parent) || IsReparsePoint(parent)) return error = "staging parent invalid", false;
    if (!CreateDirectoryW(Long(stagingDir).c_str(), nullptr)) return error = "staging directory exists or cannot be created", false;
    bool ok = false;
    struct Cleanup {
        const std::wstring& dir;
        bool& ok;
        ~Cleanup() {
            if (!ok) RemoveTree(dir);
        }
    } cleanup{stagingDir, ok};
    std::wstring root = FinalPath(stagingDir);
    if (root.empty() || IsReparsePoint(stagingDir)) return error = "staging directory unresolved", false;
    Handle in(CreateFileW(Long(file).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!in.Valid()) return error = "package reopen failed", false;
    std::vector<std::uint8_t> buffer(ChunkBytes);
    for (const auto& e : index.entries) {
        if (cancel && cancel->load()) return error = "cancelled", false;
        auto parts = Split(e.path, '/');
        std::wstring dir = stagingDir;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            dir = Join(dir, parts[i]);
            if (!CreateDirectoryW(Long(dir).c_str(), nullptr)) {
                if (GetLastError() != ERROR_ALREADY_EXISTS || !IsDirectory(dir) || IsReparsePoint(dir))
                    return error = "cannot create directory for " + e.path, false;
            }
        }
        std::wstring target = Join(stagingDir, e.path);
        Handle out(CreateFileW(Long(target).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!out.Valid()) return error = "cannot create " + e.path, false;
        if (!IsUnder(FinalPathOf(out.h), root)) return error = "entry escapes staging root: " + e.path, false;
        if (!Seek(in.h, e.offset)) return error = "package seek failed", false;
        Sha256 h;
        if (!h.Ok()) return error = "sha256 unavailable", false;
        std::uint64_t left = e.size;
        while (left) {
            if (cancel && cancel->load()) return error = "cancelled", false;
            std::size_t n = std::size_t(std::min<std::uint64_t>(left, buffer.size()));
            if (!ReadExact(in.h, buffer.data(), n)) return error = "package read failed", false;
            h.Update(buffer.data(), n);
            if (!WriteExact(out.h, buffer.data(), n)) return error = "write failed: " + e.path, false;
            left -= n;
        }
        if (h.Final() != e.sha256) return error = "entry hash mismatch: " + e.path, false;
        if (!FlushFileBuffers(out.h)) return error = "flush failed", false;
    }
    std::wstring list = Join(stagingDir, ".stage");
    {
        auto text = FormatStage(index.version, index.entries);
        Handle out(CreateFileW(Long(list).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!out.Valid() || !WriteExact(out.h, text.data(), text.size()) || !FlushFileBuffers(out.h))
            return error = "stage list write failed", false;
    }
    ok = true;
    return true;
}

namespace {
bool WalkVerify(const std::wstring& root, const std::wstring& dir, const std::string& relPrefix, StageList& list,
                std::set<std::string>& seen, std::string& error, int depth) {
    if (depth > 32) return error = "directory too deep", false;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW(Long(dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return error = "cannot list staged directory", false;
    bool ok = true;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        for (wchar_t c : name)
            if (c > 127) {
                ok = false;
                error = "unexpected non-ASCII entry";
            }
        if (!ok) break;
        std::string rel = relPrefix + Narrow(name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            ok = false;
            error = "reparse point in staged tree: " + rel;
            break;
        }
        if (depth == 0 && rel == ".stage") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!WalkVerify(root, dir + L"\\" + name, rel + "/", list, seen, error, depth + 1)) {
                ok = false;
                break;
            }
            continue;
        }
        auto key = Lower(rel);
        auto it = list.files.find(key);
        if (it == list.files.end() || !seen.insert(key).second) {
            ok = false;
            error = "unexpected file in staged tree: " + rel;
            break;
        }
        Digest d;
        std::uint64_t size = 0;
        if (!Sha256File(dir + L"\\" + name, d, size) || size != it->second.second.first || d != it->second.second.second) {
            ok = false;
            error = "staged file modified: " + rel;
            break;
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return ok;
}
}  // namespace

bool VerifyStagedTree(const std::wstring& dir, const std::string& expectedVersion, std::string& error) {
    if (!IsDirectory(dir) || IsReparsePoint(dir)) return error = "staged directory missing", false;
    std::string text;
    if (!ReadSmallFile(dir + L"\\.stage", 2 * 1024 * 1024, text)) return error = "stage list missing", false;
    StageList list;
    if (!ParseStage(text, list, error)) return false;
    if (list.version != expectedVersion) return error = "staged version mismatch", false;
    if (!list.files.count("browser.exe") || !list.files.count("update-helper.exe")) return error = "staged tree incomplete", false;
    std::set<std::string> seen;
    if (!WalkVerify(dir, dir, "", list, seen, error, 0)) return false;
    if (seen.size() != list.files.size()) return error = "staged tree is missing files", false;
    return true;
}

bool WritePackage(const std::wstring& outPath, const std::string& version, const std::string& arch,
                  const std::vector<std::pair<std::string, std::string>>& files) {
    std::string data(Magic, 8);
    auto u16 = [&](std::uint16_t v) { data.append(reinterpret_cast<const char*>(&v), 2); };
    auto u32 = [&](std::uint32_t v) { data.append(reinterpret_cast<const char*>(&v), 4); };
    auto u64 = [&](std::uint64_t v) { data.append(reinterpret_cast<const char*>(&v), 8); };
    u16(std::uint16_t(version.size()));
    data += version;
    u16(std::uint16_t(arch.size()));
    data += arch;
    u32(std::uint32_t(files.size()));
    for (const auto& f : files) {
        u16(std::uint16_t(f.first.size()));
        data += f.first;
        u64(f.second.size());
        auto d = Sha256Of(f.second);
        data.append(reinterpret_cast<const char*>(d.data()), 32);
        data += f.second;
    }
    data.append(Trailer, 8);
    Handle out(CreateFileW(Long(outPath).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    return out.Valid() && WriteExact(out.h, data.data(), data.size());
}

// ---------------------------------------------------------------- filesystem helpers

std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
bool ReadSmallFile(const std::wstring& path, std::uint64_t maxBytes, std::string& out) {
    Handle f(CreateFileW(Long(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!f.Valid()) return false;
    std::uint64_t size = 0;
    if (!FileSize(f.h, size) || size > maxBytes) return false;
    out.assign(std::size_t(size), '\0');
    return size == 0 || ReadExact(f.h, out.data(), std::size_t(size));
}
bool AtomicWriteFile(const std::wstring& path, const std::string& content) {
    std::wstring temp = path + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    {
        Handle f(CreateFileW(Long(temp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!f.Valid()) return false;
        if (!WriteExact(f.h, content.data(), content.size()) || !FlushFileBuffers(f.h)) {
            f.Close();
            DeleteFileW(Long(temp).c_str());
            return false;
        }
    }
    if (!MoveFileExW(Long(temp).c_str(), Long(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(Long(temp).c_str());
        return false;
    }
    return true;
}
bool IsReparsePoint(const std::wstring& path) {
    DWORD a = GetFileAttributesW(Long(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool PathExists(const std::wstring& path) { return GetFileAttributesW(Long(path).c_str()) != INVALID_FILE_ATTRIBUTES; }
bool IsDirectory(const std::wstring& path) {
    DWORD a = GetFileAttributesW(Long(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool RemoveTree(const std::wstring& path) {
    DWORD a = GetFileAttributesW(Long(path).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(Long(path).c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileW(Long(path).c_str()) != 0;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return RemoveDirectoryW(Long(path).c_str()) != 0;  // unlink only
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW(Long(path + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    bool ok = true;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            ok = RemoveTree(path + L"\\" + name) && ok;
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    return RemoveDirectoryW(Long(path).c_str()) != 0 && ok;
}
bool MoveDirectory(const std::wstring& from, const std::wstring& to, unsigned long timeoutMs, int* attempts,
                   const std::atomic<bool>* cancel) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (int n = 1;; ++n) {
        if (attempts) *attempts = n;
        if (MoveFileExW(Long(from).c_str(), Long(to).c_str(), 0)) return true;
        const DWORD e = GetLastError();
        if ((e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION && e != ERROR_LOCK_VIOLATION) ||
            GetTickCount64() >= deadline || (cancel && cancel->load()) || !IsDirectory(from) || PathExists(to)) {
            SetLastError(e);
            return false;
        }
        Sleep(100);
    }
}
bool IsUnder(const std::wstring& child, const std::wstring& root) {
    auto norm = [](const std::wstring& p) {
        std::wstring in = StripFinalPrefix(p);
        wchar_t buffer[4096];
        DWORD n = GetFullPathNameW(in.c_str(), 4096, buffer, nullptr);
        if (!n || n >= 4096) return std::wstring();
        std::wstring s(buffer, n);
        while (s.size() > 3 && s.back() == L'\\') s.pop_back();
        return LowerW(s);
    };
    auto c = norm(child), r = norm(root);
    if (c.empty() || r.empty() || c.size() <= r.size() + 1) return false;
    return c.compare(0, r.size(), r) == 0 && c[r.size()] == L'\\';
}
std::wstring FinalPath(const std::wstring& path) {
    Handle h(CreateFileW(Long(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!h.Valid()) return L"";
    return FinalPathOf(h.h);
}

}  // namespace UpdatePackage
