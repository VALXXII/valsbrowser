// Agent runtime: see agent-runtime.h and design/chat-acp.md.
//
// Portions adapted from Gui (Apache-2.0, https://github.com/brijr/gui, commit 0af5e38), rewritten in C++ and
// modified: src/main/agent-binaries.ts (the CLI version rule `compatible`, the staging-then-rename install),
// src/main/node-runtime.ts (pinned official Node.js, checked then moved into place) and src/main/agent-status.ts
// (which sign-in method is offered first) and src/main/agents.ts (which agent mode stands for Ask and Auto). Windows detection, the archive readers and the pinned dependency tree
// are new. Licence and notice: third_party/gui/LICENSE, third_party/gui/NOTICE.
#include "agent-runtime.h"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cstring>

#include "strict-json.h"

#ifdef BROWSER_HAS_BUILD_INFO
#include "build-info.h"
#endif

namespace AgentRuntime {
namespace {

namespace SJ = StrictJson;
#include "agent-runtime-pins.inc"

// ---------------------------------------------------------------- text and paths
std::wstring Widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(std::size_t(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring StripSlash(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}
// Long-path form for file calls (extracted packages can pass MAX_PATH under a deep data folder).
std::wstring Long(const std::wstring& p) {
    if (p.size() >= 3 && p[1] == L':' && p[2] == L'\\') return L"\\\\?\\" + p;
    return p;
}
bool IsAbsolute(const std::wstring& p) {
    return (p.size() >= 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) || p.compare(0, 2, L"\\\\") == 0;
}
bool FileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(Long(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(Long(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
std::uint64_t FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(Long(p).c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    return (std::uint64_t(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}
bool MakeDirs(const std::wstring& path) {
    if (DirExists(path)) return true;
    for (std::size_t i = 3; i <= path.size(); ++i)
        if (i == path.size() || path[i] == L'\\') {
            const std::wstring part = path.substr(0, i);
            if (!CreateDirectoryW(Long(part).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
    return true;
}
// Removes a folder we created. A link found inside is removed as a link, never followed.
void RemoveTree(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(Long(path).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return;
    if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(Long(path).c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) {
        DeleteFileW(Long(path).c_str());
        return;
    }
    if (!(a & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(Long(path + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = fd.cFileName;
                if (name != L"." && name != L"..") RemoveTree(path + L"\\" + name);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    RemoveDirectoryW(Long(path).c_str());
}
std::vector<std::wstring> ListDir(const std::wstring& path) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Long(path + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring name = fd.cFileName;
        if (name != L"." && name != L"..") out.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}
bool OlderThan(const std::wstring& path, unsigned seconds) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(Long(path).c_str(), GetFileExInfoStandard, &d)) return false;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const std::uint64_t n = (std::uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    const std::uint64_t w = (std::uint64_t(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
    return n > w && (n - w) / 10000000ull > seconds;
}
std::string RandomTag() {
    unsigned char bytes[6] = {};
    BCryptGenRandom(nullptr, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (unsigned char b : bytes) {
        s += hex[b >> 4];
        s += hex[b & 15];
    }
    return s;
}
std::string WinError(const char* what, DWORD code = GetLastError()) { return std::string(what) + " " + std::to_string(code); }
bool Cancelled(const std::atomic<bool>* cancel) { return cancel && cancel->load(); }
PrepareResult Fail(PrepareError error, std::string detail) {
    PrepareResult r;
    r.error = error;
    r.detail = std::move(detail);
    return r;
}

// ---------------------------------------------------------------- hashing
class Hasher {
public:
    explicit Hasher(bool sha512) : size_(sha512 ? 64 : 32) {
        if (BCryptOpenAlgorithmProvider(&alg_, sha512 ? BCRYPT_SHA512_ALGORITHM : BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) alg_ = nullptr;
        if (alg_ && BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0) != 0) hash_ = nullptr;
    }
    ~Hasher() {
        if (hash_) BCryptDestroyHash(hash_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;
    void Add(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        while (hash_ && size) {
            const ULONG n = ULONG(std::min<std::size_t>(size, 1u << 30));
            if (BCryptHashData(hash_, const_cast<PUCHAR>(p), n, 0) != 0) ok_ = false;
            p += n;
            size -= n;
        }
    }
    // Lowercase hex (sha256) or base64 (sha512, as npm's integrity); "" on failure.
    std::string Finish(bool base64) {
        unsigned char digest[64] = {};
        if (!hash_ || !ok_ || BCryptFinishHash(hash_, digest, ULONG(size_), 0) != 0) return {};
        std::string out;
        if (!base64) {
            static const char* hex = "0123456789abcdef";
            for (std::size_t i = 0; i < size_; ++i) {
                out += hex[digest[i] >> 4];
                out += hex[digest[i] & 15];
            }
            return out;
        }
        static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (std::size_t i = 0; i < size_; i += 3) {
            const unsigned b0 = digest[i], b1 = i + 1 < size_ ? digest[i + 1] : 0, b2 = i + 2 < size_ ? digest[i + 2] : 0;
            out += table[b0 >> 2];
            out += table[((b0 & 3) << 4) | (b1 >> 4)];
            out += i + 1 < size_ ? table[((b1 & 15) << 2) | (b2 >> 6)] : '=';
            out += i + 2 < size_ ? table[b2 & 63] : '=';
        }
        return out;
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::size_t size_;
    bool ok_ = true;
};

// ---------------------------------------------------------------- inflate (RFC 1951)
struct BitReader {
    const unsigned char* start;
    const unsigned char* p;
    const unsigned char* end;
    std::uint64_t buf = 0;
    int count = 0;
    bool bad = false;
    BitReader(const unsigned char* data, std::size_t size) : start(data), p(data), end(data + size) {}
    void Fill() {
        while (count <= 56 && p < end) {
            buf |= std::uint64_t(*p++) << count;
            count += 8;
        }
    }
    std::uint32_t Peek(int n) {
        if (count < n) Fill();
        return std::uint32_t(buf & ((std::uint64_t(1) << n) - 1));
    }
    void Drop(int n) {
        if (n > count) {  // reading past the end of the input
            bad = true;
            buf = 0;
            count = 0;
            return;
        }
        buf >>= n;
        count -= n;
    }
    std::uint32_t Take(int n) {
        const std::uint32_t v = Peek(n);
        Drop(n);
        return v;
    }
    std::size_t Consumed() const { return std::size_t(p - start) - std::size_t(count / 8); }
};
struct Huffman {
    static constexpr int FastBits = 10;
    std::uint16_t fast[1 << FastBits];  // (symbol << 4) | length ; 0 = a longer code
    std::uint16_t count[16];
    std::uint16_t symbol[288];
    bool Build(const std::uint8_t* lengths, int n) {
        std::memset(count, 0, sizeof count);
        std::memset(fast, 0, sizeof fast);
        for (int i = 0; i < n; ++i) ++count[lengths[i]];
        count[0] = 0;
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;  // more codes than the lengths allow
        }
        std::uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = std::uint16_t(offs[len] + count[len]);
        for (int s = 0; s < n; ++s)
            if (lengths[s]) symbol[offs[lengths[s]]++] = std::uint16_t(s);
        unsigned next[16] = {};
        unsigned code = 0;
        for (int len = 1; len <= 15; ++len) {
            code = (code + count[len - 1]) << 1;
            next[len] = code;
        }
        for (int s = 0; s < n; ++s) {
            const int len = lengths[s];
            if (!len) continue;
            const unsigned c = next[len]++;
            if (len > FastBits) continue;
            unsigned rev = 0;
            for (int b = 0; b < len; ++b) rev |= ((c >> b) & 1u) << (len - 1 - b);
            for (unsigned k = rev; k < (1u << FastBits); k += 1u << len) fast[k] = std::uint16_t((s << 4) | len);
        }
        return true;
    }
    int Decode(BitReader& in) const {
        const std::uint32_t bits = in.Peek(15);
        const unsigned e = fast[bits & ((1u << FastBits) - 1)];
        if (e) {
            in.Drop(int(e & 15));
            return int(e >> 4);
        }
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= int((bits >> (len - 1)) & 1u);
            const int c = count[len];
            if (code - c < first) {
                in.Drop(len);
                return symbol[index + (code - first)];
            }
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};
using Sink = std::function<bool(const char* data, std::size_t size)>;
// Inflates into `out`. With a sink, `out` is only a window: everything but the last 32 KiB is handed over as it
// grows, and the rest at the end. `consumed` receives the input bytes used.
bool InflateStream(const unsigned char* data, std::size_t size, std::string& out, std::uint64_t maxOut, std::size_t* consumed, const Sink* sink) {
    static const std::uint16_t lengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const std::uint8_t lengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const std::uint16_t distBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const std::uint8_t distExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    constexpr std::size_t Window = 32768, FlushAt = 4u << 20;
    BitReader in(data, size);
    std::uint64_t flushed = 0;
    auto lenCode = std::make_unique<Huffman>();
    auto distCode = std::make_unique<Huffman>();
    const auto flush = [&](bool all) {
        if (!sink) return true;
        const std::size_t keep = all ? 0 : Window;
        if (out.size() <= keep) return true;
        const std::size_t n = out.size() - keep;
        if (!(*sink)(out.data(), n)) return false;
        flushed += n;
        out.erase(0, n);
        return true;
    };
    for (bool final = false; !final;) {
        final = in.Take(1) != 0;
        const unsigned type = in.Take(2);
        if (in.bad) return false;
        if (type == 0) {
            in.Drop(in.count & 7);
            const unsigned len = in.Take(16), nlen = in.Take(16);
            if (in.bad || len != ((~nlen) & 0xFFFFu)) return false;
            if (flushed + out.size() + len > maxOut) return false;
            for (unsigned i = 0; i < len; ++i) out.push_back(char(in.Take(8)));
            if (in.bad) return false;
        } else if (type == 1 || type == 2) {
            std::uint8_t lengths[320];
            if (type == 1) {
                int i = 0;
                for (; i < 144; ++i) lengths[i] = 8;
                for (; i < 256; ++i) lengths[i] = 9;
                for (; i < 280; ++i) lengths[i] = 7;
                for (; i < 288; ++i) lengths[i] = 8;
                if (!lenCode->Build(lengths, 288)) return false;
                for (i = 0; i < 30; ++i) lengths[i] = 5;
                if (!distCode->Build(lengths, 30)) return false;
            } else {
                static const std::uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                const int nlen = int(in.Take(5)) + 257, ndist = int(in.Take(5)) + 1, ncode = int(in.Take(4)) + 4;
                if (in.bad || nlen > 286 || ndist > 30) return false;
                std::uint8_t codeLengths[19] = {};
                for (int i = 0; i < ncode; ++i) codeLengths[order[i]] = std::uint8_t(in.Take(3));
                Huffman meta;
                if (!meta.Build(codeLengths, 19)) return false;
                int index = 0;
                while (index < nlen + ndist) {
                    const int sym = meta.Decode(in);
                    if (sym < 0 || in.bad) return false;
                    if (sym < 16) {
                        lengths[index++] = std::uint8_t(sym);
                        continue;
                    }
                    int repeat, value = 0;
                    if (sym == 16) {
                        if (!index) return false;
                        value = lengths[index - 1];
                        repeat = 3 + int(in.Take(2));
                    } else if (sym == 17) {
                        repeat = 3 + int(in.Take(3));
                    } else {
                        repeat = 11 + int(in.Take(7));
                    }
                    if (index + repeat > nlen + ndist) return false;
                    while (repeat--) lengths[index++] = std::uint8_t(value);
                }
                if (in.bad || !lengths[256]) return false;  // no end-of-block code
                if (!lenCode->Build(lengths, nlen) || !distCode->Build(lengths + nlen, ndist)) return false;
            }
            for (;;) {
                int sym = lenCode->Decode(in);
                if (sym < 0 || in.bad) return false;
                if (sym < 256) {
                    out.push_back(char(sym));
                } else if (sym == 256) {
                    break;
                } else {
                    sym -= 257;
                    if (sym >= 29) return false;
                    const std::size_t len = lengthBase[sym] + in.Take(lengthExtra[sym]);
                    const int d = distCode->Decode(in);
                    if (d < 0 || d >= 30) return false;
                    const std::size_t dist = distBase[d] + in.Take(distExtra[d]);
                    if (in.bad || dist > out.size()) return false;  // the window always holds the last 32 KiB
                    const std::size_t pos = out.size();
                    out.resize(pos + len);
                    char* dst = &out[pos];
                    const char* src = dst - dist;
                    for (std::size_t i = 0; i < len; ++i) dst[i] = src[i];
                }
                if (out.size() >= FlushAt) {
                    if (flushed + out.size() > maxOut) return false;
                    if (!flush(false)) return false;
                }
            }
        } else {
            return false;
        }
        if (flushed + out.size() > maxOut) return false;
    }
    if (in.bad) return false;
    if (consumed) *consumed = in.Consumed();
    return flush(true);
}

std::uint32_t Le32(const unsigned char* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
std::uint16_t Le16(const unsigned char* p) { return std::uint16_t(p[0] | (p[1] << 8)); }

// A file opened for reading as one read-only block of memory.
struct Mapped {
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
    const unsigned char* data = nullptr;
    std::size_t size = 0;
    explicit Mapped(const std::wstring& path) {
        file = CreateFileW(Long(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER n{};
        if (!GetFileSizeEx(file, &n) || n.QuadPart <= 0 || n.QuadPart > (std::int64_t(1) << 31)) return;
        mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) return;
        data = static_cast<const unsigned char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (data) size = std::size_t(n.QuadPart);
    }
    ~Mapped() {
        if (data) UnmapViewOfFile(data);
        if (mapping) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    Mapped(const Mapped&) = delete;
    Mapped& operator=(const Mapped&) = delete;
};

bool WriteAll(HANDLE f, const char* data, std::size_t size) {
    while (size) {
        DWORD put = 0;
        if (!WriteFile(f, data, DWORD(std::min<std::size_t>(size, 1u << 24)), &put, nullptr) || !put) return false;
        data += put;
        size -= put;
    }
    return true;
}

// One path component of an archive entry, safe to create under our folder on Windows.
bool SafeComponent(std::string_view c) {
    if (c.empty() || c == "." || c == ".." || c.size() > 200) return false;
    if (c.back() == ' ' || c.back() == '.') return false;  // Windows would silently trim it
    for (const char ch : c) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (u < 0x20 || ch == ':' || ch == '\\' || ch == '/' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') return false;
    }
    // Device names, with or without an extension.
    std::string stem(c.substr(0, c.find('.')));
    for (char& ch : stem)
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return false;
    if (stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) && stem[3] >= '0' && stem[3] <= '9') return false;
    return SJ::ValidUtf8(c);
}
// "a/b/c" -> L"a\\b\\c" when every component is safe; `strip` leading components are dropped first.
bool SafeRelative(std::string_view path, std::size_t strip, std::wstring& out) {
    out.clear();
    std::size_t start = 0, index = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        const std::string_view part = path.substr(start, end - start);
        start = end + 1;
        if (part.empty() && end == path.size()) break;  // trailing slash of a directory entry
        if (!SafeComponent(part)) return false;
        if (index++ < strip) continue;
        if (!out.empty()) out += L'\\';
        out += Widen(part);
    }
    return true;
}

// ---------------------------------------------------------------- processes
// Runs `exe args` hidden with the user's environment plus `extra`; stdin and stderr on NUL, stdout captured (at
// most 64 KiB) or discarded. Returns the exit code, or -1 (could not start, timed out).
int RunHidden(const Environment& env, const std::wstring& exe, const std::vector<std::wstring>& args, unsigned timeoutMs, std::string* captured) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul == INVALID_HANDLE_VALUE) return -1;
    HANDLE outR = nullptr, outW = nullptr;
    if (captured && !CreatePipe(&outR, &outW, &sa, 1u << 16)) {
        CloseHandle(nul);
        return -1;
    }
    if (outR) SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<unsigned char> attrBuffer(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
    HANDLE inherit[2] = {nul, outW};
    int code = -1;
    if (attrSize && InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) {
        if (UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, (outW ? 2 : 1) * sizeof(HANDLE), nullptr, nullptr)) {
            STARTUPINFOEXW si{};
            si.StartupInfo.cb = sizeof si;
            si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            si.StartupInfo.hStdInput = nul;
            si.StartupInfo.hStdOutput = outW ? outW : nul;
            si.StartupInfo.hStdError = nul;
            si.lpAttributeList = attrs;
            std::wstring cmd = Acp::QuoteArgument(exe);
            for (const auto& a : args) cmd += L" " + Acp::QuoteArgument(a);
            Acp::LaunchSpec spec;
            spec.freshUserEnvironment = env.freshUserEnvironment;
            std::wstring block = Acp::BuildEnvironmentBlock(spec);
            PROCESS_INFORMATION pi{};
            const std::wstring cwd = DirExists(env.userProfile) ? env.userProfile : std::wstring();
            if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, block.data(),
                               cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi)) {
                if (job) AssignProcessToJobObject(job, pi.hProcess);
                ResumeThread(pi.hThread);
                CloseHandle(pi.hThread);
                if (outW) {
                    CloseHandle(outW);
                    outW = nullptr;
                }
                const DWORD until = GetTickCount() + timeoutMs;
                bool exited = false;
                for (;;) {
                    if (outR) {  // drain what is there, so a chatty CLI never blocks on a full pipe
                        DWORD available = 0;
                        while (PeekNamedPipe(outR, nullptr, 0, nullptr, &available, nullptr) && available) {
                            char chunk[4096];
                            DWORD got = 0;
                            if (!ReadFile(outR, chunk, sizeof chunk, &got, nullptr) || !got) break;
                            if (captured->size() < (64u << 10)) captured->append(chunk, got);
                        }
                    }
                    if (exited) break;
                    const int left = int(until - GetTickCount());
                    if (left <= 0) break;
                    exited = WaitForSingleObject(pi.hProcess, DWORD(std::min(left, 15))) == WAIT_OBJECT_0;
                }
                DWORD exitCode = DWORD(-1);
                if (exited && GetExitCodeProcess(pi.hProcess, &exitCode)) code = int(exitCode);
                else TerminateProcess(pi.hProcess, 1);
                CloseHandle(pi.hProcess);
            }
        }
        DeleteProcThreadAttributeList(attrs);
    }
    if (outW) CloseHandle(outW);
    if (outR) CloseHandle(outR);
    if (job) CloseHandle(job);  // kills anything the command left running
    CloseHandle(nul);
    return code;
}

std::vector<std::wstring> PathDirs(const std::wstring& path) {
    std::vector<std::wstring> out;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find(L';', start);
        if (end == std::wstring::npos) end = path.size();
        std::wstring dir = path.substr(start, end - start);
        start = end + 1;
        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') dir = dir.substr(1, dir.size() - 2);
        if (IsAbsolute(dir)) out.push_back(StripSlash(dir));  // absolute entries only: never the current directory
    }
    return out;
}
std::wstring Var(const std::vector<std::wstring>& block, const wchar_t* name) {
    const std::size_t n = wcslen(name);
    for (const auto& e : block)
        if (e.size() > n && e[n] == L'=' && _wcsnicmp(e.c_str(), name, n) == 0) return e.substr(n + 1);
    return {};
}

// ---------------------------------------------------------------- HTTPS
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};
// One WinHTTP session for a whole preparation: connections to the same host are reused.
class Http {
public:
    Http() {
        session_.h = WinHttpOpen(L"browser-agent-runtime", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_.h) session_.h = WinHttpOpen(L"browser-agent-runtime", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_.h) return;
        WinHttpSetTimeouts(session_.h, 15000, 15000, 30000, 30000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000 /* TLS 1.3 */;
        if (!WinHttpSetOption(session_.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(session_.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        }
    }
    // GET over https only. The body goes to `sink` while it is hashed; more than maxBytes is refused.
    // `restart` is called before each new attempt so the caller can drop what the failed one wrote. A network
    // failure is tried three times (a registry time-out is common); a wrong size or fingerprint never is.
    PrepareResult Get(const std::wstring& url, std::uint64_t maxBytes, bool sha512, std::string& digest, const Sink& sink,
                      const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>* cancel,
                      const std::function<bool()>& restart) {
        PrepareResult result;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (attempt) {
                for (int i = 0; i < attempt * 15 && !Cancelled(cancel); ++i) Sleep(100);
                if (Cancelled(cancel)) return Fail(PrepareError::Cancelled, "");
                if (!restart()) return Fail(PrepareError::Disk, WinError("restart"));
            }
            result = GetOnce(url, maxBytes, sha512, digest, sink, progress, cancel);
            if (result.error != PrepareError::Network) break;
        }
        return result;
    }

private:
    PrepareResult GetOnce(const std::wstring& url, std::uint64_t maxBytes, bool sha512, std::string& digest, const Sink& sink,
                          const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>* cancel) {
        const std::string name = Narrow(url);
        if (!session_.h) return Fail(PrepareError::Network, WinError("WinHttpOpen"));
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof parts;
        wchar_t host[256], path[2048];
        parts.lpszHostName = host;
        parts.dwHostNameLength = 256;
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = 2048;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return Fail(PrepareError::Network, "url " + name);
        Handle connection, request;
        connection.h = WinHttpConnect(session_.h, host, parts.nPort, 0);
        if (!connection.h) return Fail(PrepareError::Network, WinError("WinHttpConnect") + " " + name);
        request.h = WinHttpOpenRequest(connection.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request.h) return Fail(PrepareError::Network, WinError("WinHttpOpenRequest") + " " + name);
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
        if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.h, nullptr))
            return Fail(PrepareError::Network, WinError("WinHttpSendRequest") + " " + name);
        DWORD status = 0, statusSize = sizeof status;
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) return Fail(PrepareError::Network, "http " + std::to_string(status) + " " + name);
        std::uint64_t total = 0;
        wchar_t lengthText[32];
        DWORD lengthSize = sizeof lengthText;
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX))
            total = _wcstoui64(lengthText, nullptr, 10);
        if (total > maxBytes) return Fail(PrepareError::Integrity, "size " + std::to_string(total) + " " + name);
        Hasher hasher(sha512);
        std::vector<char> chunk(64u << 10);
        std::uint64_t received = 0;
        for (;;) {
            if (Cancelled(cancel)) return Fail(PrepareError::Cancelled, "");
            DWORD got = 0;
            if (!WinHttpReadData(request.h, chunk.data(), DWORD(chunk.size()), &got)) return Fail(PrepareError::Network, WinError("WinHttpReadData") + " " + name);
            if (!got) break;
            received += got;
            if (received > maxBytes) return Fail(PrepareError::Integrity, "size " + name);
            hasher.Add(chunk.data(), got);
            if (!sink(chunk.data(), got)) return Fail(PrepareError::Disk, WinError("write") + " " + name);
            if (progress) progress(received, total);
        }
        if (total && received != total) return Fail(PrepareError::Network, "truncated " + name);
        digest = hasher.Finish(sha512);
        if (digest.empty()) return Fail(PrepareError::Integrity, "hash " + name);
        return PrepareResult();
    }

    Handle session_;
};

// Calls `progress` at most every 100 ms (and always for the last call of a stage).
struct Throttle {
    const std::function<void(const Progress&)>& progress;
    DWORD last = 0;
    void Report(const Progress& p, bool force = false) {
        if (!progress) return;
        const DWORD now = GetTickCount();
        if (!force && now - last < 100) return;
        last = now;
        progress(p);
    }
};

std::wstring TarballUrl(const Environment& env, const PackagePin& pin) {
    const std::string name = pin.name;
    const std::string base = name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1);
    return env.registry + L"/" + Widen(name) + L"/-/" + Widen(base) + L"-" + Widen(pin.version) + L".tgz";
}
std::wstring Backslashes(std::string_view path) {
    std::wstring w = Widen(path);
    std::replace(w.begin(), w.end(), L'/', L'\\');
    return w;
}
// Leftovers of an interrupted preparation (a quit or a crash in the middle), old enough not to be someone's work.
void RemoveLeftovers(const std::wstring& root) {
    for (const auto& name : ListDir(root))
        if (!name.empty() && name[0] == L'.' && OlderThan(root + L"\\" + name, 3600)) RemoveTree(root + L"\\" + name);
}
bool NodeReady(const Environment& env) {
    const NodePin& pin = env.ThePins().node;
    return FileSize(NodeExe(env)) == pin.exeBytes;
}
bool AdapterReady(const Environment& env, Agent agent) { return FileExists(AdapterEntry(env, agent)); }

PrepareResult PrepareNode(const Environment& env, Http& http, Throttle& report, const std::atomic<bool>* cancel) {
    const NodePin& pin = env.ThePins().node;
    const std::wstring root = AgentsRoot(env) + L"\\node";
    if (!MakeDirs(root)) return Fail(PrepareError::Disk, WinError("mkdir"));
    RemoveLeftovers(root);
    const std::wstring tag = Widen(RandomTag());
    const std::wstring zip = root + L"\\.download-" + tag + L".zip", staging = root + L"\\.unpack-" + tag;
    struct Cleanup {
        std::wstring zip, staging;
        ~Cleanup() {
            DeleteFileW(Long(zip).c_str());
            RemoveTree(staging);
        }
    } cleanup{zip, staging};
    Progress p;
    p.stage = Progress::Stage::Node;
    p.total = pin.zipBytes;
    report.Report(p, true);
    {
        HANDLE f = CreateFileW(Long(zip).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return Fail(PrepareError::Disk, WinError("create"));
        std::string digest;
        const Sink sink = [&](const char* data, std::size_t size) { return WriteAll(f, data, size); };
        const PrepareResult got = http.Get(env.nodeDist + L"/v" + Widen(pin.version) + L"/" + Widen(pin.zipName), pin.zipBytes, false, digest, sink,
                                           [&](std::uint64_t received, std::uint64_t) {
                                               p.received = received;
                                               report.Report(p);
                                           },
                                           cancel, [&] {
                                               return SetFilePointer(f, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER && SetEndOfFile(f) != 0;
                                           });
        const bool flushed = FlushFileBuffers(f) != 0;
        CloseHandle(f);
        if (!got.Ok()) return got;
        if (!flushed) return Fail(PrepareError::Disk, WinError("flush"));
        if (digest != pin.zipSha256 || FileSize(zip) != pin.zipBytes) return Fail(PrepareError::Integrity, std::string(pin.zipName) + " sha256 " + digest);
    }
    p.received = p.total;
    report.Report(p, true);
    if (Cancelled(cancel)) return Fail(PrepareError::Cancelled, "");
    if (!MakeDirs(staging)) return Fail(PrepareError::Disk, WinError("mkdir"));
    // Only node.exe (and its licence) is taken: npm is not used, so its thousands of files stay in the zip.
    std::string folder = pin.zipName;
    folder.resize(folder.size() - 4);
    std::string why;
    if (!detail::ExtractZipFiles(zip, {{folder + "/node.exe", staging + L"\\node.exe"}, {folder + "/LICENSE", staging + L"\\LICENSE"}}, why))
        return Fail(PrepareError::Archive, std::string(pin.zipName) + ": " + why);
    std::string exeDigest;
    if (!detail::HashFile(staging + L"\\node.exe", false, exeDigest) || exeDigest != pin.exeSha256 || FileSize(staging + L"\\node.exe") != pin.exeBytes)
        return Fail(PrepareError::Integrity, "node.exe sha256 " + exeDigest);
    const std::wstring target = NodeDir(env);
    if (!MoveFileExW(Long(staging).c_str(), Long(target).c_str(), MOVEFILE_WRITE_THROUGH) && !NodeReady(env)) {
        RemoveTree(target);  // an incomplete folder from another version of this code
        if (!MoveFileExW(Long(staging).c_str(), Long(target).c_str(), MOVEFILE_WRITE_THROUGH)) return Fail(PrepareError::Disk, WinError("rename"));
    }
    return PrepareResult();
}

PrepareResult PrepareAdapter(const Environment& env, Agent agent, Http& http, Throttle& report, const std::atomic<bool>* cancel) {
    const AdapterPin& pin = env.ThePins().Adapter(agent);
    const std::wstring root = AgentsRoot(env) + L"\\adapters\\" + Widen(AgentId(agent));
    if (!MakeDirs(root)) return Fail(PrepareError::Disk, WinError("mkdir"));
    RemoveLeftovers(root);
    const std::wstring staging = root + L"\\.unpack-" + Widen(RandomTag());
    struct Cleanup {
        std::wstring staging;
        ~Cleanup() { RemoveTree(staging); }
    } cleanup{staging};
    if (!MakeDirs(staging)) return Fail(PrepareError::Disk, WinError("mkdir"));
    Progress p;
    p.stage = Progress::Stage::Adapter;
    p.itemCount = pin.packageCount;
    report.Report(p, true);
    for (std::size_t i = 0; i < pin.packageCount; ++i) {
        const PackagePin& package = pin.packages[i];
        if (Cancelled(cancel)) return Fail(PrepareError::Cancelled, "");
        std::string tarball, digest;
        const Sink sink = [&](const char* data, std::size_t size) {
            tarball.append(data, size);
            return true;
        };
        p.item = i;
        p.received = p.total = 0;
        const PrepareResult got = http.Get(TarballUrl(env, package), 64u << 20, true, digest, sink,
                                           [&](std::uint64_t received, std::uint64_t total) {
                                               p.received = received;
                                               p.total = total;
                                               report.Report(p);
                                           },
                                           cancel, [&] {
                                               tarball.clear();
                                               return true;
                                           });
        if (!got.Ok()) return got;
        if ("sha512-" + digest != package.integrity) return Fail(PrepareError::Integrity, std::string(package.name) + "@" + package.version + " sha512-" + digest);
        std::wstring relative;
        if (!SafeRelative(package.path, 0, relative)) return Fail(PrepareError::Archive, std::string("path ") + package.path);
        const std::wstring dir = staging + L"\\" + relative;
        std::string why;
        if (!MakeDirs(dir) || !detail::ExtractNpmTarball(tarball, dir, why)) return Fail(PrepareError::Archive, std::string(package.name) + ": " + why);
        // The package is what the pin says it is (its own manifest agrees with the table).
        std::string manifestText;
        {
            Mapped manifest(dir + L"\\package.json");
            if (manifest.data && manifest.size <= (4u << 20)) manifestText.assign(reinterpret_cast<const char*>(manifest.data), manifest.size);
        }
        SJ::Limits limits;
        limits.maxBytes = 4u << 20;
        limits.maxDepth = 64;
        limits.allowBom = true;
        SJ::Value manifest;
        const SJ::Value* name = SJ::Parse(manifestText, manifest, nullptr, limits) ? manifest.Get("name") : nullptr;
        const SJ::Value* version = manifest.Get("version");
        if (!name || !name->IsString() || name->string != package.name || !version || !version->IsString() || version->string != package.version)
            return Fail(PrepareError::Integrity, std::string(package.name) + " manifest");
    }
    p.item = pin.packageCount;
    report.Report(p, true);
    if (!FileExists(staging + L"\\" + Backslashes(pin.entry))) return Fail(PrepareError::Archive, std::string("entry ") + pin.entry);
    if (Cancelled(cancel)) return Fail(PrepareError::Cancelled, "");
    const std::wstring target = AdapterDir(env, agent);
    if (!MoveFileExW(Long(staging).c_str(), Long(target).c_str(), MOVEFILE_WRITE_THROUGH) && !AdapterReady(env, agent)) {
        RemoveTree(target);
        if (!MoveFileExW(Long(staging).c_str(), Long(target).c_str(), MOVEFILE_WRITE_THROUGH)) return Fail(PrepareError::Disk, WinError("rename"));
    }
    return PrepareResult();
}

const wchar_t* CliName(Agent agent) { return agent == Agent::Claude ? L"claude.exe" : L"codex.exe"; }

// Where an npm global install keeps the native binary behind its claude.cmd / codex.cmd shim.
std::vector<std::wstring> NpmNative(const std::wstring& npmDir, Agent agent) {
    const std::wstring modules = npmDir + L"\\node_modules\\";
    if (agent == Agent::Claude)
        return {modules + L"@anthropic-ai\\claude-code\\bin\\claude.exe",
                modules + L"@anthropic-ai\\claude-code\\node_modules\\@anthropic-ai\\claude-code-win32-x64\\claude.exe",
                modules + L"@anthropic-ai\\claude-code-win32-x64\\claude.exe"};
    const std::wstring vendor = L"\\vendor\\x86_64-pc-windows-msvc\\bin\\codex.exe";
    return {modules + L"@openai\\codex\\node_modules\\@openai\\codex-win32-x64" + vendor, modules + L"@openai\\codex-win32-x64" + vendor,
            modules + L"@openai\\codex" + vendor};
}

}  // namespace

// ================================================================ identity, pins, paths
const char* AgentId(Agent agent) { return agent == Agent::Claude ? "claude" : "codex"; }
bool ParseAgentId(std::string_view id, Agent& agent) {
    if (id == "claude") agent = Agent::Claude;
    else if (id == "codex") agent = Agent::Codex;
    else return false;
    return true;
}
const wchar_t* AgentDisplayName(Agent agent) {
    return agent == Agent::Claude ? L"Claude Code" : L"Codex";  // i18n-exempt: product names
}
const Pins& BuiltInPins() { return kBuiltInPins; }

std::wstring AgentsRoot(const Environment& env) { return StripSlash(env.dataRoot) + L"\\agents"; }
std::wstring NodeDir(const Environment& env) { return AgentsRoot(env) + L"\\node\\" + Widen(env.ThePins().node.version); }
std::wstring NodeExe(const Environment& env) { return NodeDir(env) + L"\\node.exe"; }
std::wstring AdapterDir(const Environment& env, Agent agent) {
    const AdapterPin& pin = env.ThePins().Adapter(agent);
    return AgentsRoot(env) + L"\\adapters\\" + Widen(AgentId(agent)) + L"\\" + Widen(pin.version) + L"-" + Widen(pin.treeDigest);
}
std::wstring AdapterEntry(const Environment& env, Agent agent) {
    return AdapterDir(env, agent) + L"\\" + Backslashes(env.ThePins().Adapter(agent).entry);
}

Environment Environment::FromProcess(const std::wstring& dataRoot) {
    Environment env;
    env.dataRoot = dataRoot;
    wchar_t probe[8];
    SetLastError(ERROR_SUCCESS);
    // Under test the process environment is the one the test built (redirected folders): it is used as it is.
    const bool test = GetEnvironmentVariableW(L"BROWSER_ANNOTATIONS_TEST", probe, 8) != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
    env.freshUserEnvironment = !test;
    Acp::LaunchSpec spec;
    spec.freshUserEnvironment = env.freshUserEnvironment;
    const std::wstring block = Acp::BuildEnvironmentBlock(spec);
    std::vector<std::wstring> entries;
    for (const wchar_t* p = block.c_str(); *p; p += wcslen(p) + 1) entries.emplace_back(p);
    env.path = Var(entries, L"PATH");
    env.userProfile = Var(entries, L"USERPROFILE");
    env.appData = Var(entries, L"APPDATA");
    env.localAppData = Var(entries, L"LOCALAPPDATA");
    if (test) {
        // Test guard (see Environment::agentsRefused): the fake agent, or nothing — never this computer's agents unless
        // BROWSER_CHAT_REAL_AGENT=1 asks for them by name.
        const auto refuse = [&env](std::string why) {
            env.agentsRefused = true;
            env.refusedWhy = std::move(why);
        };
        const std::wstring fake = Var(entries, L"BROWSER_CHAT_FAKE_AGENT");
        if (!fake.empty()) {
#ifndef BROWSER_CHANNEL_RELEASE
            int count = 0;
            std::wstring named;
            if (wchar_t** argv = CommandLineToArgvW(fake.c_str(), &count)) {
                if (count > 0) named = argv[0];
                if (count > 0 && IsAbsolute(named) && FileExists(named)) {
                    env.fakeAgentExe = named;
                    for (int i = 1; i < count; ++i) env.fakeAgentArgs.emplace_back(argv[i]);
                }
                LocalFree(argv);
            }
            if (env.fakeAgentExe.empty())
                refuse("BROWSER_CHAT_FAKE_AGENT names no program found at an absolute path: \"" + Narrow(named) + "\"");
#else
            refuse("BROWSER_CHAT_FAKE_AGENT is set but a release build has no fake-agent hook");
#endif
        } else if (Var(entries, L"BROWSER_CHAT_REAL_AGENT") != L"1") {
            refuse("BROWSER_ANNOTATIONS_TEST without BROWSER_CHAT_FAKE_AGENT (BROWSER_CHAT_REAL_AGENT=1 asks for this computer's agents)");
        }
    }
    return env;
}

// ================================================================ detection
namespace detail {

bool ParseVersion(std::string_view text, int out[3]) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9' || (i > 0 && ((text[i - 1] >= '0' && text[i - 1] <= '9') || text[i - 1] == '.'))) continue;
        std::size_t p = i;
        int parts[3] = {0, 0, 0};
        int n = 0;
        for (; n < 3; ++n) {
            const std::size_t digits = p;
            while (p < text.size() && text[p] >= '0' && text[p] <= '9' && p - digits < 9) parts[n] = parts[n] * 10 + (text[p++] - '0');
            if (p == digits) break;
            if (n < 2) {
                if (p >= text.size() || text[p] != '.') break;
                ++p;
            }
        }
        if (n == 3) {
            out[0] = parts[0];
            out[1] = parts[1];
            out[2] = parts[2];
            return true;
        }
    }
    return false;
}

// Claude Code stays on the release line the Claude Agent SDK was built against (same major.minor, patch at least
// the SDK's); a newer line is allowed but flagged. Codex takes any CLI at or above the adapter's minimum.
CliState Compatible(Agent agent, std::string_view found, const AdapterPin& pin) {
    int have[3], min[3], line[3];
    if (!ParseVersion(found, have) || !ParseVersion(pin.minCliVersion, min) || !ParseVersion(pin.cliLine, line)) return CliState::Untested;
    int order = 0;
    for (int i = 0; i < 3 && !order; ++i) order = have[i] < min[i] ? -1 : have[i] > min[i] ? 1 : 0;
    if (order < 0) return CliState::TooOld;
    if (agent == Agent::Claude && (have[0] != line[0] || have[1] != line[1])) return CliState::Untested;
    return CliState::Found;
}

std::vector<std::pair<std::wstring, std::string>> CliCandidates(const Environment& env, Agent agent) {
    std::vector<std::pair<std::wstring, std::string>> out;
    const auto add = [&](const std::wstring& path, const char* source) {
        if (!IsAbsolute(path) || !FileExists(path)) return;
        for (const auto& c : out)
            if (CompareStringOrdinal(c.first.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL) return;
        out.emplace_back(path, source);
    };
    const std::wstring name = CliName(agent);
    const std::wstring shim = agent == Agent::Claude ? L"claude.cmd" : L"codex.cmd";
    // 1. Where the official installers put it, in the user's own folders (SEC-15: before PATH, whose folders may be
    //    writable by another account; Detect still prefers a compatible CLI found later to an incompatible one here).
    if (!env.userProfile.empty() && agent == Agent::Claude) {
        add(env.userProfile + L"\\.local\\bin\\claude.exe", "installer");
        add(env.userProfile + L"\\.claude\\local\\claude.exe", "installer");
    }
    if (!env.localAppData.empty()) {
        if (agent == Agent::Codex) add(env.localAppData + L"\\Programs\\OpenAI\\Codex\\bin\\codex.exe", "installer");
        add(env.localAppData + L"\\Microsoft\\WinGet\\Links\\" + name, "winget");
    }
    // 2. The user's PATH: what they would get in a terminal.
    for (const auto& dir : PathDirs(env.path)) {
        add(dir + L"\\" + name, "path");
        // An npm shim is never run (cmd.exe parsing of a path cannot be made safe): the native binary behind it is.
        if (FileExists(dir + L"\\" + shim))
            for (const auto& native : NpmNative(dir, agent)) add(native, "npm");
    }
    // 3. npm's default global folder.
    if (!env.appData.empty())
        for (const auto& native : NpmNative(env.appData + L"\\npm", agent)) add(native, "npm");
    return out;
}

bool SignInArgsAllowed(Agent agent, const std::vector<std::wstring>& args) {
    std::size_t i = 0;
    if (agent == Agent::Claude) {
        if (args.size() < 2 || args[0] != L"auth" || args[1] != L"login") return false;
        i = 2;
    } else {
        if (args.empty() || args[0] != L"login") return false;
        i = 1;
    }
    if (args.size() > i + 4) return false;
    for (; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        if (a.size() < 3 || a.size() > 40 || a[0] != L'-' || a[1] != L'-' || !(a[2] >= L'a' && a[2] <= L'z')) return false;
        for (std::size_t j = 3; j < a.size(); ++j)
            if (!((a[j] >= L'a' && a[j] <= L'z') || (a[j] >= L'0' && a[j] <= L'9') || a[j] == L'-')) return false;
    }
    return true;
}

}  // namespace detail

Detection Detect(const Environment& env, Agent agent, const DetectOptions& options) {
    Detection d;
    d.agent = agent;
    if (env.agentsRefused) {  // test guard: no CLI is looked for, none is run
        d.cli.source = "refused";
        return d;
    }
    if (!env.fakeAgentExe.empty()) {  // test hook: the fake agent stands for both agents
        d.cli.state = CliState::Found;
        d.cli.path = env.fakeAgentExe;
        d.cli.source = "test";
        d.login = Login::SignedIn;
        d.nodeReady = d.adapterReady = true;
        return d;
    }
    d.nodeReady = NodeReady(env);
    d.adapterReady = AdapterReady(env, agent);
    const AdapterPin& pin = env.ThePins().Adapter(agent);
    const std::vector<std::wstring> versionArgs(1, std::wstring(L"--version"));
    for (const auto& candidate : detail::CliCandidates(env, agent)) {
        std::string output;
        const int code = RunHidden(env, candidate.first, versionArgs, options.timeoutMs, &output);
        int version[3];
        if (code != 0 || !detail::ParseVersion(output, version)) continue;  // not a working CLI: try the next one
        const std::string text = std::to_string(version[0]) + "." + std::to_string(version[1]) + "." + std::to_string(version[2]);
        const CliState state = detail::Compatible(agent, text, pin);
        if (d.cli.state == CliState::Missing || state == CliState::Found) {  // the first that answers, unless a later one is compatible
            d.cli.path = candidate.first;
            d.cli.source = candidate.second;
            d.cli.version = text;
            d.cli.state = state;
        }
        if (state == CliState::Found) break;
    }
    if (d.Usable() && options.checkLogin) {
        // The CLI's own documented status command; its output is discarded, only the exit code is read.
        std::vector<std::wstring> args;
        args.emplace_back(agent == Agent::Claude ? L"auth" : L"login");
        args.emplace_back(L"status");
        const int code = RunHidden(env, d.cli.path, args, options.timeoutMs, nullptr);
        d.login = code == 0 ? Login::SignedIn : code == 1 ? Login::SignedOut : Login::Unknown;
    }
    return d;
}

InstallHelp GetInstallHelp(Agent agent) {
    // i18n-exempt-begin: official command lines and addresses, shown as they are for the user to copy
    if (agent == Agent::Claude)
        return {L"irm https://claude.ai/install.ps1 | iex", L"winget install Anthropic.ClaudeCode", L"claude update",
                L"https://code.claude.com/docs/en/setup"};
    return {L"npm install -g @openai/codex", L"", L"npm install -g @openai/codex@latest", L"https://github.com/openai/codex#quickstart"};
    // i18n-exempt-end
}

// ================================================================ preparation
const char* PrepareErrorName(PrepareError error) {
    switch (error) {
    case PrepareError::None: return "none";
    case PrepareError::Cancelled: return "cancelled";
    case PrepareError::Network: return "network";
    case PrepareError::Integrity: return "integrity";
    case PrepareError::Disk: return "disk";
    case PrepareError::Archive: return "archive";
    case PrepareError::Refused: return "refused";
    }
    return "none";
}

PrepareResult Prepare(const Environment& env, Agent agent, const std::function<void(const Progress&)>& progress, const std::atomic<bool>* cancel) {
    if (env.agentsRefused) return Fail(PrepareError::Refused, env.refusedWhy);  // test guard: nothing downloaded
    Throttle report{progress};
    Progress p;
    report.Report(p, true);
    if (env.fakeAgentExe.empty() && (!NodeReady(env) || !AdapterReady(env, agent))) {
        if (!IsAbsolute(env.dataRoot)) return Fail(PrepareError::Disk, "dataRoot");
        Http http;
        if (!NodeReady(env)) {
            const PrepareResult node = PrepareNode(env, http, report, cancel);
            if (!node.Ok()) return node;
        }
        if (!AdapterReady(env, agent)) {
            const PrepareResult adapter = PrepareAdapter(env, agent, http, report, cancel);
            if (!adapter.Ok()) return adapter;
        }
    }
    p.stage = Progress::Stage::Done;
    report.Report(p, true);
    return PrepareResult();
}

void RemoveStale(const Environment& env) {
    const std::wstring nodeRoot = AgentsRoot(env) + L"\\node";
    const std::wstring nodeKeep = Widen(env.ThePins().node.version);
    for (const auto& name : ListDir(nodeRoot))
        if (name[0] == L'.' ? OlderThan(nodeRoot + L"\\" + name, 3600) : _wcsicmp(name.c_str(), nodeKeep.c_str()) != 0) RemoveTree(nodeRoot + L"\\" + name);
    for (const Agent agent : {Agent::Claude, Agent::Codex}) {
        const std::wstring keep = AdapterDir(env, agent);
        const std::wstring root = keep.substr(0, keep.rfind(L'\\'));
        for (const auto& name : ListDir(root)) {
            const std::wstring path = root + L"\\" + name;
            if (name[0] == L'.' ? OlderThan(path, 3600) : _wcsicmp(path.c_str(), keep.c_str()) != 0) RemoveTree(path);
        }
    }
}

// ================================================================ launch
Acp::Resolution ResolveLaunch(const Environment& env, Agent agent, const Acp::ResolveContext& context) {
    Acp::Resolution r;
    if (env.agentsRefused) {  // test guard: no process is created
        r.error.code = Acp::ErrorCode::NotInstalled;
        r.error.detail = "refused (test guard): " + env.refusedWhy;
        return r;
    }
    if (!env.fakeAgentExe.empty()) {
        r.launch.exe = env.fakeAgentExe;
        r.launch.args = env.fakeAgentArgs;
        r.launch.freshUserEnvironment = false;
        if (!context.signIn) r.launch.env.emplace_back(L"BROWSER", L"none");
        return r;
    }
    DetectOptions quick;
    quick.checkLogin = false;  // the session start says whether the user is signed in
    const Detection d = Detect(env, agent, quick);
    if (d.cli.state == CliState::Missing) {
        r.error.code = Acp::ErrorCode::NotInstalled;
        return r;
    }
    if (d.cli.state == CliState::TooOld) {
        r.error.code = Acp::ErrorCode::CliTooOld;
        r.error.detail = d.cli.version;
        return r;
    }
    if (d.NeedsPrepare()) {
        r.error.code = Acp::ErrorCode::NeedsPrepare;
        return r;
    }
    // SEC-15: node.exe is run only as the pinned build. The size check above is for detection; before each launch its
    // SHA-256 is checked again (a portable install in a shared folder could have been replaced since the download). A
    // mismatch removes it: the next preparation downloads it again.
    {
        std::string digest;
        if (!detail::HashFile(NodeExe(env), false, digest) || digest != env.ThePins().node.exeSha256) {
            RemoveTree(NodeDir(env));
            r.error.code = Acp::ErrorCode::NeedsPrepare;
            r.error.detail = "node.exe sha256";
            return r;
        }
    }
    r.launch.exe = NodeExe(env);
    r.launch.args = {AdapterEntry(env, agent)};
    r.launch.freshUserEnvironment = env.freshUserEnvironment;
    // The adapter drives the user's own CLI instead of the platform build it would otherwise need (370-500 MB).
    r.launch.env.emplace_back(agent == Agent::Claude ? L"CLAUDE_CODE_EXECUTABLE" : L"CODEX_PATH", d.cli.path);
    if (agent == Agent::Claude) r.launch.env.emplace_back(L"CLAUDE_AGENT_ACP_PROTOCOL", L"v1");
    // Dev servers and tools the agent starts must not open the user's web browser; signing in needs it.
    if (!context.signIn) r.launch.env.emplace_back(L"BROWSER", L"none");
    return r;
}

Acp::Resolver MakeResolver(Environment env, Agent agent) {
    return [env = std::move(env), agent](const Acp::ResolveContext& context) { return ResolveLaunch(env, agent, context); };
}

ModeMap GetModeMap(Agent agent) {
    ModeMap m;
    if (agent == Agent::Claude) {
        m.ask = {"default"};
        m.automatic = {"auto", "acceptEdits"};
        m.planMode = "plan";
        m.never = {"bypassPermissions"};
    } else {
        m.ask = {"read-only"};
        m.automatic = {"agent", "workspace-write"};
        m.planOption = "collaboration_mode";
        m.planOn = "plan";
        m.planOff = "default";
        m.never = {"agent-full-access"};
    }
    return m;
}

// ================================================================ terminal sign-in
SignIn::~SignIn() { Close(); }
SignIn::SignIn(SignIn&& other) noexcept : process_(other.process_), job_(other.job_) { other.process_ = other.job_ = nullptr; }
SignIn& SignIn::operator=(SignIn&& other) noexcept {
    if (this != &other) {
        Close();
        process_ = other.process_;
        job_ = other.job_;
        other.process_ = other.job_ = nullptr;
    }
    return *this;
}
bool SignIn::Wait(unsigned timeoutMs, int* exitCode) {
    if (!process_) return true;
    if (WaitForSingleObject(process_, timeoutMs) != WAIT_OBJECT_0) return false;
    DWORD code = DWORD(-1);
    GetExitCodeProcess(process_, &code);
    if (exitCode) *exitCode = int(code);
    return true;
}
void SignIn::Close() {
    if (job_) {
        TerminateJobObject(job_, 1);
        CloseHandle(job_);
    }
    if (process_) CloseHandle(process_);
    process_ = job_ = nullptr;
}

SignIn RunTerminalSignIn(const Environment& env, Agent agent, const Acp::AuthMethod& method, std::string* error) {
    SignIn result;
    const auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return std::move(result);
    };
    if (env.agentsRefused) return fail("refused");  // test guard: no console, no CLI
    if (!method.terminal) return fail("not_terminal");
    std::wstring exe;
    std::vector<std::wstring> args;
    if (!env.fakeAgentExe.empty()) {
        exe = env.fakeAgentExe;
        args = env.fakeAgentArgs;
        for (const auto& a : method.terminalArgs) args.push_back(Widen(a));
    } else {
        DetectOptions quick;
        quick.checkLogin = false;
        const Detection d = Detect(env, agent, quick);
        if (!d.Usable()) return fail("not_installed");
        exe = d.cli.path;
        // The adapter's own "--cli" hands the rest to its CLI: we run that CLI directly.
        for (std::size_t i = 0; i < method.terminalArgs.size(); ++i)
            if (i > 0 || method.terminalArgs[i] != "--cli") args.push_back(Widen(method.terminalArgs[i]));
        // SEC-15: the agent names the sign-in, it does not choose the command line: only the CLI's own sign-in shapes.
        if (!detail::SignInArgsAllowed(agent, args)) {
            args.clear();
            for (const auto& a : DefaultTerminalSignIn(agent).terminalArgs) args.push_back(Widen(a));
        }
    }
    Acp::LaunchSpec spec;
    spec.freshUserEnvironment = env.freshUserEnvironment;
    // SEC-15: the agent's `env` for the sign-in is not applied (the CLI's sign-in needs none; it could redirect the CLI).
    std::wstring block = Acp::BuildEnvironmentBlock(spec);
    std::wstring cmd = Acp::QuoteArgument(exe);
    for (const auto& a : args) cmd += L" " + Acp::QuoteArgument(a);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const std::wstring cwd = DirExists(env.userProfile) ? env.userProfile : std::wstring();
    // A console of its own, visible: the CLI says what it is doing and opens the user's web browser.
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                        block.data(), cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
        const DWORD code = GetLastError();
        if (job) CloseHandle(job);
        return fail(WinError("CreateProcess", code));
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    result.process_ = pi.hProcess;
    result.job_ = job;
    return result;
}

Acp::AuthMethod DefaultTerminalSignIn(Agent agent) {
    Acp::AuthMethod m;
    m.terminal = true;
    if (agent == Agent::Claude) {
        m.id = "claude-ai-login";
        m.terminalArgs = {"auth", "login", "--claudeai"};
    } else {
        m.id = "chat-gpt";
        m.terminalArgs = {"login"};
    }
    return m;
}

const Acp::AuthMethod* PreferredSignIn(const std::vector<Acp::AuthMethod>& methods) {
    const auto lower = [](std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        return s;
    };
    // Account sign-ins that open the web browser, in the order the agents name them.
    for (const char* known : {"claude-ai-login", "chat-gpt", "chatgpt", "browser", "oauth", "claude-ai", "login"})
        for (const auto& m : methods)
            if (lower(m.id) == known) return &m;
    for (const auto& m : methods) {
        const std::string id = lower(m.id);
        if (id.find("apikey") == std::string::npos && id.find("api-key") == std::string::npos && id.find("api_key") == std::string::npos) return &m;
    }
    return nullptr;
}

// ================================================================ archives, hashing, download
namespace detail {

std::string Sha256Hex(const void* data, std::size_t size) {
    Hasher h(false);
    h.Add(data, size);
    return h.Finish(false);
}
std::string Sha512Base64(const void* data, std::size_t size) {
    Hasher h(true);
    h.Add(data, size);
    return h.Finish(true);
}
bool HashFile(const std::wstring& path, bool sha512, std::string& out) {
    HANDLE f = CreateFileW(Long(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    Hasher h(sha512);
    std::vector<char> chunk(1u << 20);
    DWORD got = 0;
    bool ok = true;
    while ((ok = ReadFile(f, chunk.data(), DWORD(chunk.size()), &got, nullptr) != 0) && got) h.Add(chunk.data(), got);
    CloseHandle(f);
    out = h.Finish(sha512);
    return ok && !out.empty();
}

std::uint32_t Crc32(const void* data, std::size_t size, std::uint32_t crc) {
    static std::uint32_t table[256];
    static const bool ready = [] {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return true;
    }();
    (void)ready;
    const auto* p = static_cast<const unsigned char*>(data);
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool Inflate(const unsigned char* in, std::size_t inSize, std::string& out, std::size_t maxOut) {
    out.clear();
    return InflateStream(in, inSize, out, maxOut, nullptr, nullptr);
}

bool ExtractZipFiles(const std::wstring& zip, const std::vector<std::pair<std::string, std::wstring>>& wanted, std::string& error) {
    const auto fail = [&](const char* why) {
        error = why;
        return false;
    };
    Mapped file(zip);
    if (!file.data || file.size < 22) return fail("open");
    // End of central directory: last 22 bytes plus an optional comment.
    std::size_t eocd = file.size;
    for (std::size_t i = file.size - 22, tries = 0; tries <= 65535; --i, ++tries) {
        if (Le32(file.data + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
        if (!i) break;
    }
    if (eocd == file.size) return fail("no_directory");
    const std::size_t entries = Le16(file.data + eocd + 10), directorySize = Le32(file.data + eocd + 12), directory = Le32(file.data + eocd + 16);
    if (entries == 0xFFFF || directory == 0xFFFFFFFFu || directory > file.size || directorySize > file.size - directory) return fail("zip64");
    std::vector<bool> done(wanted.size(), false);
    std::size_t at = directory;
    for (std::size_t n = 0; n < entries; ++n) {
        if (at + 46 > directory + directorySize || Le32(file.data + at) != 0x02014b50u) return fail("directory_entry");
        const unsigned flags = Le16(file.data + at + 8), method = Le16(file.data + at + 10);
        const std::uint32_t crc = Le32(file.data + at + 16), packed = Le32(file.data + at + 20), size = Le32(file.data + at + 24);
        const std::size_t nameLength = Le16(file.data + at + 28), extraLength = Le16(file.data + at + 30), commentLength = Le16(file.data + at + 32);
        const std::size_t local = Le32(file.data + at + 42);
        if (at + 46 + nameLength > directory + directorySize) return fail("directory_entry");
        const std::string_view name(reinterpret_cast<const char*>(file.data + at + 46), nameLength);
        at += 46 + nameLength + extraLength + commentLength;
        for (std::size_t w = 0; w < wanted.size(); ++w) {
            if (done[w] || name != wanted[w].first) continue;
            if ((flags & 1) || (method != 0 && method != 8) || packed == 0xFFFFFFFFu || size == 0xFFFFFFFFu) return fail("unsupported_entry");
            if (local + 30 > file.size || Le32(file.data + local) != 0x04034b50u) return fail("local_header");
            const std::size_t dataAt = local + 30 + Le16(file.data + local + 26) + Le16(file.data + local + 28);
            if (dataAt > file.size || packed > file.size - dataAt) return fail("entry_data");
            HANDLE f = CreateFileW(Long(wanted[w].second).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) return fail("create");
            std::uint32_t actualCrc = 0;
            std::uint64_t written = 0;
            bool ok;
            const Sink sink = [&](const char* data, std::size_t length) {
                actualCrc = Crc32(data, length, actualCrc);
                written += length;
                return WriteAll(f, data, length);
            };
            if (method == 0) {
                ok = packed == size && sink(reinterpret_cast<const char*>(file.data + dataAt), packed);
            } else {
                std::string window;
                ok = InflateStream(file.data + dataAt, packed, window, size, nullptr, &sink);
            }
            ok = ok && FlushFileBuffers(f) != 0;
            CloseHandle(f);
            if (!ok || written != size || actualCrc != crc) {
                DeleteFileW(Long(wanted[w].second).c_str());
                return fail("entry_does_not_match_its_checksum");
            }
            done[w] = true;
        }
    }
    for (const bool d : done)
        if (!d) return fail("entry_missing");
    return true;
}

bool ExtractNpmTarball(const std::string& tgz, const std::wstring& destDir, std::string& error) {
    const auto fail = [&](const char* why) {
        error = why;
        return false;
    };
    const auto* data = reinterpret_cast<const unsigned char*>(tgz.data());
    if (tgz.size() < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 8) return fail("not_gzip");
    const unsigned flags = data[3];
    std::size_t at = 10;
    if (flags & 4) {
        if (at + 2 > tgz.size()) return fail("gzip_header");
        at += 2 + Le16(data + at);
    }
    for (const unsigned bit : {8u, 16u})  // file name, comment: NUL-terminated
        if (flags & bit) {
            while (at < tgz.size() && data[at]) ++at;
            ++at;
        }
    if (flags & 2) at += 2;
    if (at + 8 > tgz.size()) return fail("gzip_header");
    std::string tar;
    std::size_t consumed = 0;
    if (!InflateStream(data + at, tgz.size() - at - 8, tar, 512u << 20, &consumed, nullptr)) return fail("corrupt_gzip_data");
    const unsigned char* trailer = data + at + consumed;
    if (at + consumed + 8 > tgz.size() || Le32(trailer) != Crc32(tar.data(), tar.size()) || Le32(trailer + 4) != std::uint32_t(tar.size()))
        return fail("gzip_checksum");

    std::string longName;  // from a pax "path" record or a GNU long-name entry, for the next entry
    std::size_t files = 0;
    for (std::size_t pos = 0; pos + 512 <= tar.size();) {
        const auto* h = reinterpret_cast<const unsigned char*>(tar.data() + pos);
        bool zero = true;
        for (int i = 0; i < 512 && zero; ++i) zero = h[i] == 0;
        if (zero) break;  // end of archive
        unsigned sum = 0;
        for (int i = 0; i < 512; ++i) sum += (i >= 148 && i < 156) ? 32u : h[i];
        if (std::strtoul(std::string(reinterpret_cast<const char*>(h + 148), 8).c_str(), nullptr, 8) != sum) return fail("tar_header_checksum");
        if (h[124] & 0x80) return fail("tar_entry_too_large");
        const std::uint64_t size = std::strtoull(std::string(reinterpret_cast<const char*>(h + 124), 12).c_str(), nullptr, 8);
        const std::size_t body = pos + 512;
        if (size > tar.size() - body) return fail("tar_entry_truncated");
        const std::size_t next = body + std::size_t((size + 511) & ~std::uint64_t(511));
        const char type = char(h[156]);
        const auto field = [&](std::size_t offset, std::size_t length) {
            const char* p = reinterpret_cast<const char*>(h + offset);
            return std::string(p, strnlen(p, length));
        };
        if (type == 'x' || type == 'g') {  // pax header: "<length> key=value\n" records
            if (type == 'x') {
                std::size_t r = 0;
                while (r < size) {
                    const std::string_view rest(tar.data() + body + r, std::size_t(size) - r);
                    const std::size_t space = rest.find(' ');
                    if (space == std::string_view::npos) break;
                    const std::size_t length = std::strtoul(std::string(rest.substr(0, space)).c_str(), nullptr, 10);
                    if (length <= space + 1 || length > rest.size()) return fail("pax_record");
                    const std::string_view record = rest.substr(space + 1, length - space - 2);
                    if (record.rfind("path=", 0) == 0) longName = std::string(record.substr(5));
                    else if (record.rfind("linkpath=", 0) == 0) return fail("link_in_package");
                    r += length;
                }
            }
            pos = next;
            continue;
        }
        if (type == 'L') {  // GNU long name
            longName.assign(tar.data() + body, strnlen(tar.data() + body, std::size_t(size)));
            pos = next;
            continue;
        }
        std::string name = longName;
        longName.clear();
        if (name.empty()) {
            name = field(0, 100);
            const std::string prefix = std::memcmp(h + 257, "ustar", 5) == 0 ? field(345, 155) : std::string();
            if (!prefix.empty()) name = prefix + "/" + name;
        }
        // Only plain files and directories: a link or a device has no place in an npm package.
        if (type != '0' && type != '\0' && type != '5') return fail("entry_is_not_a_file");
        std::wstring relative;
        if (!SafeRelative(name, 1, relative)) return fail("unsafe_path");
        if (!relative.empty()) {
            const std::wstring target = destDir + L"\\" + relative;
            if (type == '5') {
                if (!MakeDirs(target)) return fail("mkdir");
            } else {
                if (++files > 20000 || size > (128u << 20)) return fail("too_large");
                const std::size_t slash = target.rfind(L'\\');
                if (!MakeDirs(target.substr(0, slash))) return fail("mkdir");
                HANDLE f = CreateFileW(Long(target).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f == INVALID_HANDLE_VALUE) return fail("create");
                const bool ok = WriteAll(f, tar.data() + body, std::size_t(size));
                CloseHandle(f);
                if (!ok) return fail("write");
            }
        }
        pos = next;
    }
    return files > 0 || fail("empty_package");
}

PrepareResult Download(const std::wstring& url, const std::wstring& file, std::uint64_t maxBytes, bool sha512, std::string& digest,
                       const std::function<void(std::uint64_t received, std::uint64_t total)>& progress, const std::atomic<bool>* cancel) {
    HANDLE f = CreateFileW(Long(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return Fail(PrepareError::Disk, WinError("create"));
    Http http;
    const Sink sink = [&](const char* data, std::size_t size) { return WriteAll(f, data, size); };
    const PrepareResult r = http.Get(url, maxBytes, sha512, digest, sink, progress, cancel, [&] {
        return SetFilePointer(f, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER && SetEndOfFile(f) != 0;
    });
    CloseHandle(f);
    if (!r.Ok()) DeleteFileW(Long(file).c_str());
    return r;
}

}  // namespace detail

}  // namespace AgentRuntime
