// Agent connection (annotations V2-A). Contract: src/agent-connect.h.
// Win32 (kernel32, shell32, shlwapi, ole32) + C++17; no UI, no global state.
#include "agent-connect.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace AgentConnect {
namespace {

using StrictJson::Value;
namespace SJ = StrictJson;
namespace BA = BrowserAnnotations;

constexpr const char* BridgeFileName = "browser-annotations-mcp.exe";

// ---------------------------------------------------------------- strings
std::wstring Widen(const std::string& s) {
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
std::wstring LowerW(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(s.data(), DWORD(s.size()));
    return s;
}
bool SamePath(const std::wstring& a, const std::wstring& b) { return LowerW(a) == LowerW(b); }

std::wstring EnvVar(const wchar_t* name) {
    wchar_t small[1024];
    DWORD n = GetEnvironmentVariableW(name, small, 1024);
    if (!n) return L"";
    if (n < 1024) return std::wstring(small, n);
    std::wstring big(n, L'\0');
    n = GetEnvironmentVariableW(name, big.data(), DWORD(big.size()));
    big.resize(n < big.size() ? n : 0);
    return big;
}
std::wstring StripSlash(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}
std::wstring FullPath(const std::wstring& p) {
    if (p.empty()) return p;
    wchar_t buffer[MAX_PATH * 4];
    const DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 4, buffer, nullptr);
    return (n && n < MAX_PATH * 4) ? StripSlash(std::wstring(buffer, n)) : StripSlash(p);
}
std::wstring Parent(const std::wstring& p) {
    const auto at = p.find_last_of(L"\\/");
    return at == std::wstring::npos ? L"" : p.substr(0, at);
}

// ---------------------------------------------------------------- files
bool DirExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool FileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

enum class Read { Ok, Missing, TooBig, Error };
Read ReadAll(const std::wstring& path, std::uint64_t maxBytes, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? Read::Missing : Read::Error;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0) {
        CloseHandle(h);
        return Read::Error;
    }
    if (std::uint64_t(size.QuadPart) > maxBytes) {
        CloseHandle(h);
        return Read::TooBig;
    }
    out.resize(std::size_t(size.QuadPart));
    std::size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        if (!ReadFile(h, out.data() + done, DWORD(std::min<std::size_t>(out.size() - done, 1u << 20)), &got, nullptr) || !got) break;
        done += got;
    }
    CloseHandle(h);
    if (done != out.size()) {
        out.clear();
        return Read::Error;
    }
    return Read::Ok;
}

// Temporary file next to the target, flushed, then MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH).
bool AtomicWrite(const std::wstring& path, const std::string& data) {
    wchar_t suffix[64];
    std::swprintf(suffix, 64, L".tmp-%lu-%llu", GetCurrentProcessId(), static_cast<unsigned long long>(GetTickCount64()));
    const std::wstring temp = path + suffix;
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::size_t done = 0;
    bool ok = true;
    while (ok && done < data.size()) {
        DWORD put = 0;
        ok = WriteFile(h, data.data() + done, DWORD(std::min<std::size_t>(data.size() - done, 1u << 20)), &put, nullptr) && put;
        done += put;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

// UTC fields from BrowserAnnotations::FormatTime ("2026-11-14T15:30:12Z").
struct Utc { int year = 1980, month = 1, day = 1, hour = 0, minute = 0, second = 0; };
Utc UtcOf(std::int64_t unixSeconds) {
    const std::string t = BA::FormatTime(unixSeconds);
    Utc u;
    if (t.size() >= 19)
        std::sscanf(t.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &u.year, &u.month, &u.day, &u.hour, &u.minute, &u.second);
    return u;
}
std::wstring Stamp(std::int64_t now) {
    const Utc u = UtcOf(now);
    wchar_t buffer[32];
    std::swprintf(buffer, 32, L"%04d%02d%02d-%02d%02d%02d", u.year, u.month, u.day, u.hour, u.minute, u.second);
    return buffer;
}
// <file>.browser-annotations-<stamp>[-n].bak, never overwriting an older backup.
bool Backup(const std::wstring& file, std::int64_t now, std::wstring& backup) {
    backup.clear();
    if (!FileExists(file)) return true;
    const std::wstring base = file + L".browser-annotations-" + Stamp(now);
    for (int n = 1; n < 100; ++n) {
        const std::wstring candidate = base + (n == 1 ? L"" : L"-" + std::to_wstring(n)) + L".bak";
        if (CopyFileW(file.c_str(), candidate.c_str(), TRUE)) {
            backup = candidate;
            return true;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) return false;
    }
    return false;
}

// The write failed after the backup was taken: the original is intact, so the backup is only clutter.
void DropBackup(std::wstring& backup) {
    if (backup.empty()) return;
    SetFileAttributesW(backup.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(backup.c_str());
    backup.clear();
}

Report Make(Result r, std::string message, const std::wstring& file = L"", const std::wstring& backup = L"") {
    Report rep;
    rep.result = r;
    rep.message = std::move(message);
    rep.file = file;
    rep.backup = backup;
    return rep;
}

// ---------------------------------------------------------------- JSON pretty printing + text splicing
void PrettyTo(std::string& out, const Value& v, const std::string& indent, const std::string& unit, const std::string& nl) {
    if (v.IsObject() && !v.members.empty()) {
        out += "{";
        for (std::size_t i = 0; i < v.members.size(); ++i) {
            out += (i ? "," : "") + nl + indent + unit + SJ::Serialize(Value::Str(v.members[i].first)) + ": ";
            PrettyTo(out, v.members[i].second, indent + unit, unit, nl);
        }
        out += nl + indent + "}";
    } else if (v.IsArray() && !v.items.empty()) {
        bool scalars = true;
        for (const auto& item : v.items) scalars = scalars && !item.IsObject() && !item.IsArray();
        if (scalars) {  // short arrays of strings stay on one line, like Claude's own file
            out += "[";
            for (std::size_t i = 0; i < v.items.size(); ++i) out += (i ? ", " : "") + SJ::Serialize(v.items[i]);
            out += "]";
            return;
        }
        out += "[";
        for (std::size_t i = 0; i < v.items.size(); ++i) {
            out += (i ? "," : "") + nl + indent + unit;
            PrettyTo(out, v.items[i], indent + unit, unit, nl);
        }
        out += nl + indent + "]";
    } else {
        out += SJ::Serialize(v);
    }
}
std::string Pretty(const Value& v, const std::string& indent = "", const std::string& unit = "  ", const std::string& nl = "\n") {
    std::string out;
    PrettyTo(out, v, indent, unit, nl);
    return out;
}

Value ServerValue(const Target& t) {
    Value args = Value::Arr();
    args.Push(Value::Str("--store"));
    args.Push(Value::Str(Narrow(t.storeRoot)));
    Value server = Value::Obj();
    server.Set("command", Value::Str(Narrow(t.bridgeExe)));
    server.Set("args", std::move(args));
    return server;
}

SJ::Limits ConfigLimits() {
    SJ::Limits l;
    l.maxBytes = std::size_t(MaxConfigBytes);
    l.maxDepth = 128;
    l.maxStringBytes = std::size_t(MaxConfigBytes);
    l.maxItems = 1u << 20;
    l.allowBom = true;
    return l;
}

// A scanner over text already validated by StrictJson::Parse, giving byte spans of object members.
struct Member {
    std::string key;
    std::size_t keyStart = 0, valueStart = 0, valueEnd = 0;
};
std::size_t SkipWs(const std::string& s, std::size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    return i;
}
std::size_t SkipString(const std::string& s, std::size_t i) {  // s[i] == '"'
    for (++i; i < s.size(); ++i) {
        if (s[i] == '\\') ++i;
        else if (s[i] == '"') return i + 1;
    }
    return s.size();
}
std::size_t SkipValue(const std::string& s, std::size_t i) {
    if (i >= s.size()) return i;
    if (s[i] == '"') return SkipString(s, i);
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        for (; i < s.size(); ++i) {
            if (s[i] == '"') {
                i = SkipString(s, i) - 1;
            } else if (s[i] == '{' || s[i] == '[') {
                ++depth;
            } else if (s[i] == '}' || s[i] == ']') {
                if (--depth == 0) return i + 1;
            }
        }
        return s.size();
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') ++i;
    return i;
}
// Members of the object whose '{' is at `open`; `close` = index of its '}'.
std::vector<Member> Members(const std::string& s, std::size_t open, std::size_t& close) {
    std::vector<Member> out;
    std::size_t i = SkipWs(s, open + 1);
    while (i < s.size() && s[i] != '}') {
        Member m;
        m.keyStart = i;
        const std::size_t keyEnd = SkipString(s, i);
        Value key;
        SJ::Parse(std::string_view(s).substr(i, keyEnd - i), key);
        m.key = key.string;
        i = SkipWs(s, keyEnd);
        i = SkipWs(s, i + 1);  // ':'
        m.valueStart = i;
        m.valueEnd = SkipValue(s, i);
        out.push_back(std::move(m));
        i = SkipWs(s, out.back().valueEnd);
        if (i < s.size() && s[i] == ',') i = SkipWs(s, i + 1);
    }
    close = i;
    return out;
}
// Indentation of the line holding `pos` when only whitespace precedes it on that line (true); else false and "".
bool LineIndent(const std::string& s, std::size_t pos, std::string& indent) {
    std::size_t start = pos;
    while (start > 0 && s[start - 1] != '\n') --start;
    indent = s.substr(start, pos - start);
    if (start > 0 && indent.find_first_not_of(" \t") == std::string::npos) return true;
    indent.clear();
    return false;
}

bool EntryMatches(const ConfiguredEntry& e, const Target& t) {
    return e.present && SamePath(e.command, t.bridgeExe) && e.args.size() == 2 && e.args[0] == L"--store" && SamePath(e.args[1], t.storeRoot);
}
ConfiguredEntry EntryFromJson(const Value* server) {
    ConfiguredEntry e;
    if (!server) return e;
    e.present = true;
    if (!server->IsObject()) return e;
    if (const Value* c = server->Get("command"); c && c->IsString()) e.command = Widen(c->string);
    if (const Value* a = server->Get("args"); a && a->IsArray())
        for (const auto& item : a->items)
            if (item.IsString()) e.args.push_back(Widen(item.string));
    return e;
}

// ---------------------------------------------------------------- TOML (line/block level, never re-serialized)
struct TomlHeader {
    std::size_t lineStart = 0;
    std::vector<std::string> path;
    bool array = false;
};
struct TomlKey {
    std::size_t lineStart = 0, valueStart = 0;
    std::vector<std::string> path;
    int block = -1;  // index of the header block (-1 = root)
};
struct TomlScan {
    bool ok = false;
    std::vector<TomlHeader> headers;
    std::vector<TomlKey> keys;
    std::vector<std::size_t> lineStarts;     // every line start
    std::vector<bool> lineTop;               // the line starts outside any string/array/inline table
};

bool BareKeyChar(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; }

void AppendCp(std::string& out, std::uint32_t cp) { SJ::detail::AppendUtf8(out, cp); }

// Parses one TOML string (basic "..." with escapes, or literal '...') at s[i]; i moves past it.
bool ParseTomlString(const std::string& s, std::size_t& i, std::string& out) {
    out.clear();
    if (i >= s.size()) return false;
    const char q = s[i];
    if (q == '\'') {
        if (s.compare(i, 3, "'''") == 0) return false;  // multi-line strings are not used for our keys
        const auto end = s.find('\'', i + 1);
        if (end == std::string::npos || s.find('\n', i + 1) < end) return false;
        out = s.substr(i + 1, end - i - 1);
        i = end + 1;
        return true;
    }
    if (q != '"' || s.compare(i, 3, "\"\"\"") == 0) return false;
    for (++i; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"') {
            ++i;
            return true;
        }
        if (c == '\n') return false;
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i >= s.size()) return false;
        switch (s[i]) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'b': out += '\b'; break;
        case 't': out += '\t'; break;
        case 'n': out += '\n'; break;
        case 'f': out += '\f'; break;
        case 'r': out += '\r'; break;
        case 'u':
        case 'U': {
            const std::size_t len = s[i] == 'u' ? 4 : 8;
            if (i + len >= s.size()) return false;
            std::uint32_t cp = 0;
            for (std::size_t k = 1; k <= len; ++k) {
                const char h = s[i + k];
                cp <<= 4;
                if (h >= '0' && h <= '9') cp |= std::uint32_t(h - '0');
                else if (h >= 'a' && h <= 'f') cp |= std::uint32_t(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') cp |= std::uint32_t(h - 'A' + 10);
                else return false;
            }
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
            AppendCp(out, cp);
            i += len;
            break;
        }
        default: return false;
        }
    }
    return false;
}

// Dotted key at s[i] (bare / "basic" / 'literal' segments, whitespace around dots); stops before `stop`.
bool ParseTomlKey(const std::string& s, std::size_t& i, std::vector<std::string>& path) {
    path.clear();
    for (;;) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        std::string segment;
        if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
            if (!ParseTomlString(s, i, segment)) return false;
        } else {
            const std::size_t start = i;
            while (i < s.size() && BareKeyChar(s[i])) ++i;
            if (i == start) return false;
            segment = s.substr(start, i - start);
        }
        path.push_back(std::move(segment));
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i < s.size() && s[i] == '.') {
            ++i;
            continue;
        }
        return true;
    }
}

// Scans the whole document: strings (4 kinds), comments, nesting of arrays/inline tables; records each line
// start, whether it is at top level, table headers and key lines. ok=false when the structure cannot be
// followed (unterminated string, unbalanced brackets, a top-level line that is neither key, header, comment
// nor blank): the file is then never edited.
TomlScan ScanToml(const std::string& s) {
    TomlScan scan;
    int depth = 0;
    enum class St { Normal, Basic, Literal, MlBasic, MlLiteral, Comment } st = St::Normal;
    std::size_t i = (s.compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3 : 0;
    bool lineStart = true;
    while (i <= s.size()) {
        if (lineStart) {
            lineStart = false;
            scan.lineStarts.push_back(i);
            const bool top = st == St::Normal && depth == 0;
            scan.lineTop.push_back(top);
            if (top) {
                std::size_t j = i;
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
                if (j < s.size() && s[j] == '[') {
                    TomlHeader h;
                    h.lineStart = i;
                    std::size_t k = j + 1;
                    if (k < s.size() && s[k] == '[') {
                        h.array = true;
                        ++k;
                    }
                    if (!ParseTomlKey(s, k, h.path)) return scan;
                    if (k >= s.size() || s[k] != ']' || (h.array && (k + 1 >= s.size() || s[k + 1] != ']'))) return scan;
                    scan.headers.push_back(std::move(h));
                } else if (j < s.size() && s[j] != '#' && s[j] != '\n' && s[j] != '\r') {
                    TomlKey key;
                    key.lineStart = i;
                    std::size_t k = j;
                    if (!ParseTomlKey(s, k, key.path) || k >= s.size() || s[k] != '=') return scan;
                    key.valueStart = k + 1;
                    key.block = int(scan.headers.size()) - 1;
                    scan.keys.push_back(std::move(key));
                }
            }
        }
        if (i == s.size()) break;
        const char c = s[i];
        switch (st) {
        case St::Normal:
            if (c == '#') st = St::Comment;
            else if (c == '"') {
                if (s.compare(i, 3, "\"\"\"") == 0) {
                    st = St::MlBasic;
                    i += 2;
                } else {
                    st = St::Basic;
                }
            } else if (c == '\'') {
                if (s.compare(i, 3, "'''") == 0) {
                    st = St::MlLiteral;
                    i += 2;
                } else {
                    st = St::Literal;
                }
            } else if (c == '[' || c == '{') {
                ++depth;
            } else if (c == ']' || c == '}') {
                if (--depth < 0) return scan;
            }
            break;
        case St::Comment:
            if (c == '\n') st = St::Normal;
            break;
        case St::Basic:
            if (c == '\\') ++i;
            else if (c == '"') st = St::Normal;
            else if (c == '\n') return scan;
            break;
        case St::Literal:
            if (c == '\'') st = St::Normal;
            else if (c == '\n') return scan;
            break;
        case St::MlBasic:
            if (c == '\\') ++i;
            else if (s.compare(i, 3, "\"\"\"") == 0) {
                std::size_t k = i + 3;
                while (k < s.size() && s[k] == '"' && k < i + 5) ++k;  // up to two extra quotes belong to the content
                i = k - 1;
                st = St::Normal;
            }
            break;
        case St::MlLiteral:
            if (s.compare(i, 3, "'''") == 0) {
                std::size_t k = i + 3;
                while (k < s.size() && s[k] == '\'' && k < i + 5) ++k;
                i = k - 1;
                st = St::Normal;
            }
            break;
        }
        if (i < s.size() && s[i] == '\n') lineStart = true;
        ++i;
    }
    scan.ok = st == St::Normal || st == St::Comment;
    scan.ok = scan.ok && depth == 0;
    return scan;
}

bool IsOurHeader(const TomlHeader& h, const std::string& name) {
    return h.path.size() >= 2 && h.path[0] == "mcp_servers" && h.path[1] == name;
}

// Our blocks + whether our entry also appears in a form we do not edit.
struct TomlPlan {
    bool scanned = false, foreign = false;
    std::vector<std::pair<std::size_t, std::size_t>> blocks;  // [start, contentEnd) of each of our header blocks
    int firstHeader = -1;
};
std::size_t LineIndexOf(const TomlScan& scan, std::size_t pos) {
    return std::size_t(std::upper_bound(scan.lineStarts.begin(), scan.lineStarts.end(), pos) - scan.lineStarts.begin()) - 1;
}
TomlPlan PlanToml(const std::string& s, const TomlScan& scan, const std::string& name) {
    TomlPlan plan;
    plan.scanned = scan.ok;
    if (!scan.ok) return plan;
    for (std::size_t h = 0; h < scan.headers.size(); ++h) {
        const TomlHeader& header = scan.headers[h];
        if (header.array && !header.path.empty() && header.path[0] == "mcp_servers") plan.foreign = true;
        if (header.array || !IsOurHeader(header, name)) continue;
        if (plan.firstHeader < 0) plan.firstHeader = int(h);
        const std::size_t start = header.lineStart;
        const std::size_t end = h + 1 < scan.headers.size() ? scan.headers[h + 1].lineStart : s.size();
        // Content end: after the last line of the block that is not a top-level blank/comment line (trailing
        // comments and blank lines stay with whatever follows).
        std::size_t contentEnd = start;
        for (std::size_t li = LineIndexOf(scan, start); li < scan.lineStarts.size() && scan.lineStarts[li] < end; ++li) {
            const std::size_t ls = scan.lineStarts[li];
            std::size_t le = s.find('\n', ls);
            le = le == std::string::npos ? s.size() : le + 1;
            std::size_t j = ls;
            while (j < s.size() && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r')) ++j;
            const bool trivial = scan.lineTop[li] && (j >= s.size() || s[j] == '\n' || s[j] == '#');
            if (!trivial) contentEnd = std::min(le, end);
        }
        plan.blocks.push_back({start, contentEnd});
    }
    for (const TomlKey& k : scan.keys) {
        if (k.block < 0) {
            if (!k.path.empty() && k.path[0] == "mcp_servers" && (k.path.size() == 1 || k.path[1] == name)) plan.foreign = true;
            continue;
        }
        const auto& hp = scan.headers[std::size_t(k.block)].path;
        if (hp.size() == 1 && hp[0] == "mcp_servers" && !k.path.empty() && k.path[0] == name) plan.foreign = true;
    }
    return plan;
}

bool ParseTomlValueString(const std::string& s, std::size_t i, std::string& out) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return ParseTomlString(s, i, out);
}
bool ParseTomlStringArray(const std::string& s, std::size_t i, std::vector<std::string>& out) {
    out.clear();
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i >= s.size() || s[i] != '[') return false;
    ++i;
    for (;;) {
        while (i < s.size()) {  // whitespace, newlines, comments
            if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') ++i;
            else if (s[i] == '#') {
                while (i < s.size() && s[i] != '\n') ++i;
            } else break;
        }
        if (i >= s.size()) return false;
        if (s[i] == ']') return true;
        std::string item;
        if (!ParseTomlString(s, i, item)) return false;
        out.push_back(std::move(item));
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        if (i < s.size() && s[i] == ',') ++i;
    }
}

ConfiguredEntry EntryFromToml(const std::string& s, const TomlScan& scan, const std::string& name) {
    ConfiguredEntry e;
    for (const TomlKey& k : scan.keys) {
        if (k.block < 0 || k.path.size() != 1) continue;
        const TomlHeader& h = scan.headers[std::size_t(k.block)];
        if (h.array || h.path.size() != 2 || !IsOurHeader(h, name)) continue;
        e.present = true;
        std::string value;
        std::vector<std::string> list;
        if (k.path[0] == "command" && ParseTomlValueString(s, k.valueStart, value)) e.command = Widen(value);
        if (k.path[0] == "args" && ParseTomlStringArray(s, k.valueStart, list))
            for (const auto& a : list) e.args.push_back(Widen(a));
    }
    for (const TomlHeader& h : scan.headers)
        if (!h.array && h.path.size() == 2 && IsOurHeader(h, name)) e.present = true;
    return e;
}

std::string TomlBasic(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20 || c == 0x7F) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04X", c);
            out += buffer;
        } else {
            out += char(c);
        }
    }
    return out + "\"";
}
std::string TomlKeySegment(const std::string& k) {
    bool bare = !k.empty();
    for (char c : k) bare = bare && BareKeyChar(c);
    return bare ? k : TomlBasic(k);
}
std::string TomlBlock(const Target& t, const std::string& nl) {
    return "[mcp_servers." + TomlKeySegment(t.serverName) + "]" + nl + "command = " + TomlBasic(Narrow(t.bridgeExe)) + nl +
           "args = [\"--store\", " + TomlBasic(Narrow(t.storeRoot)) + "]" + nl;
}

std::string NewlineOf(const std::string& s) { return s.find("\r\n") != std::string::npos ? "\r\n" : "\n"; }
bool EndsWith(const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; }

// Removes [a, b) without leaving two blank lines, a leading blank line or trailing blank lines at the seam
// (so "connect then disconnect" gives the original file back).
void EraseBlock(std::string& s, std::size_t a, std::size_t b, const std::string& nl) {
    s.erase(a, b - a);
    const std::string two = nl + nl;
    auto blankBefore = [&] { return a == 0 || (a >= two.size() && s.compare(a - two.size(), two.size(), two) == 0); };
    while (a < s.size() && s.compare(a, nl.size(), nl) == 0 && blankBefore()) s.erase(a, nl.size());
    if (a >= s.size())
        while (EndsWith(s, two)) s.erase(s.size() - nl.size());
}

// ---------------------------------------------------------------- processes
// Runs `exe args` hidden, stdio on NUL, with CODEX_HOME overridden; returns the exit code or -1.
int RunHidden(const std::wstring& exe, const std::vector<std::wstring>& args, const std::wstring& codexHome, const std::wstring& cwd,
              unsigned timeoutMs) {
    std::wstring cmd = QuoteArg(exe);
    for (const auto& a : args) cmd += L" " + QuoteArg(a);
    // Environment block: the current one with CODEX_HOME replaced.
    std::wstring block;
    if (wchar_t* envs = GetEnvironmentStringsW()) {
        for (const wchar_t* p = envs; *p; p += wcslen(p) + 1) {
            const std::wstring var = p;
            if (LowerW(var.substr(0, 11)) == L"codex_home=") continue;
            block += var;
            block.push_back(L'\0');
        }
        FreeEnvironmentStringsW(envs);
    }
    block += L"CODEX_HOME=" + codexHome;
    block.push_back(L'\0');
    block.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul == INVALID_HANDLE_VALUE) return -1;
    // Only the NUL handle is inherited (explicit handle list), never other inheritable handles of the browser.
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<unsigned char> attrBuffer(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
    HANDLE inherit[1] = {nul};
    const bool attrsOk = attrSize && InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                         UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr);
    if (!attrsOk) {
        CloseHandle(nul);
        return -1;
    }
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = nul;
    si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;
    const BOOL started = CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT, block.data(),
                                        cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(nul);
    if (!started) return -1;
    DWORD code = DWORD(-1);
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &code);
    } else {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        code = DWORD(-1);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return int(code);
}

// ---------------------------------------------------------------- Claude extension lookup (read-only)
bool InstalledExtension(const Environment& env, const std::string& name, ConfiguredEntry* entry) {
    const std::wstring dir = ClaudeConfigDir(env);
    if (dir.empty()) return false;
    const std::wstring extensions = dir + L"\\Claude Extensions";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((extensions + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        const std::wstring leaf = fd.cFileName;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || leaf == L"." || leaf == L"..") continue;
        std::string text;
        if (ReadAll(extensions + L"\\" + leaf + L"\\manifest.json", 1u << 20, text) != Read::Ok) continue;
        Value v;
        if (!SJ::Parse(text, v, nullptr, ConfigLimits()) || !v.IsObject()) continue;
        const Value* n = v.Get("name");
        if (!n || !n->IsString() || n->string != name) continue;
        found = true;
        if (entry) {
            const Value* server = v.Get("server");
            const Value* config = server && server->IsObject() ? server->Get("mcp_config") : nullptr;
            *entry = EntryFromJson(config);
        }
    } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

}  // namespace

// ================================================================ environment
Environment Environment::FromProcess() {
    Environment e;
    {   const std::wstring args = LowerW(EnvVar(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS"));
        e.testMode = !EnvVar(L"BROWSER_ANNOTATIONS_TEST").empty() || args.find(L"remote-debugging") != std::wstring::npos; }
    e.protectedRoots = [] {
        std::vector<std::wstring> roots;
        auto known = [](REFKNOWNFOLDERID id) {
            PWSTR p = nullptr; std::wstring out;
            if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &p)) && p) out = p;
            CoTaskMemFree(p); return out;
        };
        // KNOWNFOLDERIDs spelled out (no -luuid needed by the tests and tools that link this file).
        static const GUID roamingId = {0x3EB685DB, 0x65F9, 0x4CF6, {0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D}};
        static const GUID localId = {0xF1B32785, 0x6FBA, 0x4FCF, {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};
        static const GUID profileId = {0x5E6C858F, 0x0E22, 0x4760, {0x9A, 0xFE, 0xEA, 0x33, 0x17, 0xB6, 0x71, 0x73}};
        const std::wstring roaming = known(roamingId), local = known(localId), profile = known(profileId);
        if (!roaming.empty()) roots.push_back(roaming + L"\\Claude");
        if (!local.empty()) roots.push_back(local + L"\\Packages");
        if (!profile.empty()) roots.push_back(profile + L"\\.codex");
        return roots;
    };
    e.appData = EnvVar(L"APPDATA");
    e.localAppData = EnvVar(L"LOCALAPPDATA");
    e.userProfile = EnvVar(L"USERPROFILE");
    e.codexHome = EnvVar(L"CODEX_HOME");
    e.path = EnvVar(L"PATH");
    e.driveType = [](const std::wstring& root) { return unsigned(GetDriveTypeW(root.c_str())); };
    e.now = [] { return BA::SystemNow(); };
    e.mcpbAssociated = [] {
        for (ASSOCSTR what : {ASSOCSTR_EXECUTABLE, ASSOCSTR_COMMAND, ASSOCSTR_DELEGATEEXECUTE}) {
            wchar_t buffer[MAX_PATH * 2];
            DWORD n = MAX_PATH * 2;
            if (AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN | ASSOCF_NOTRUNCATE, what, L".mcpb", L"open", buffer, &n) == S_OK && n > 1)
                return true;
        }
        return false;
    };
    e.openFile = [](const std::wstring& file) {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof info;
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"open";
        info.lpFile = file.c_str();
        info.nShow = SW_SHOWNORMAL;
        const bool ok = ShellExecuteExW(&info) != FALSE;
        if (SUCCEEDED(com)) CoUninitialize();
        return ok;
    };
    return e;
}

// ================================================================ locations
std::wstring ClaudeConfigDir(const Environment& env) {
    // MSIX (Microsoft Store) Claude Desktop reads its virtualized copy when one exists.
    if (!env.localAppData.empty()) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((env.localAppData + L"\\Packages\\Claude_*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            std::wstring found;
            do {
                const std::wstring dir = env.localAppData + L"\\Packages\\" + fd.cFileName + L"\\LocalCache\\Roaming\\Claude";
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && DirExists(dir)) found = dir;
            } while (found.empty() && FindNextFileW(h, &fd));
            FindClose(h);
            if (!found.empty()) return found;
        }
    }
    if (!env.appData.empty() && DirExists(env.appData + L"\\Claude")) return StripSlash(env.appData) + L"\\Claude";
    return L"";
}
std::wstring ClaudeConfigPath(const Environment& env) {
    const std::wstring dir = ClaudeConfigDir(env);
    return dir.empty() ? L"" : dir + L"\\claude_desktop_config.json";
}
std::wstring CodexHome(const Environment& env) {
    if (!env.codexHome.empty()) return StripSlash(env.codexHome);
    if (!env.userProfile.empty()) return StripSlash(env.userProfile) + L"\\.codex";
    return L"";
}
std::wstring CodexConfigPath(const Environment& env) {
    const std::wstring home = CodexHome(env);
    return home.empty() ? L"" : home + L"\\config.toml";
}
std::wstring FindCodexCli(const Environment& env) {
    std::size_t start = 0;
    while (start <= env.path.size()) {
        auto end = env.path.find(L';', start);
        if (end == std::wstring::npos) end = env.path.size();
        std::wstring dir = env.path.substr(start, end - start);
        start = end + 1;
        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') dir = dir.substr(1, dir.size() - 2);
        // Absolute entries only (X:\... or \\server\...): never the current directory.
        const bool absolute = (dir.size() >= 3 && dir[1] == L':' && (dir[2] == L'\\' || dir[2] == L'/')) || dir.compare(0, 2, L"\\\\") == 0;
        if (!absolute) continue;
        const std::wstring candidate = StripSlash(dir) + L"\\codex.exe";
        if (FileExists(candidate)) return candidate;
    }
    return L"";
}
Detection Detect(const Environment& env) {
    Detection d;
    d.claudeConfig = ClaudeConfigPath(env);
    d.claude = !d.claudeConfig.empty();
    const std::wstring home = CodexHome(env);
    d.codex = !home.empty() && DirExists(home);
    d.codexConfig = d.codex ? home + L"\\config.toml" : L"";
    d.codexCli = FindCodexCli(env);
    d.mcpbAssociated = env.mcpbAssociated && env.mcpbAssociated();
    return d;
}

BridgeLocation LocateBridge(const std::wstring& browserExe, BridgeLayout layout, const std::wstring& installRoot) {
    const std::wstring exe = FullPath(browserExe), dir = Parent(exe);
    std::wstring root = installRoot;
    if (root.empty()) {
        const std::wstring local = EnvVar(L"LOCALAPPDATA");
        if (!local.empty()) root = local + L"\\browser";
    }
    root = FullPath(root);
    BridgeLocation out;
    if (!root.empty() && SamePath(Parent(dir), root + L"\\versions")) {   // installed: the stable copy, present or not
        out.path = root + L"\\bin\\" + Widen(BridgeFileName);
        out.installed = true;
        return out;
    }
    out.path = dir + (layout == BridgeLayout::AssetsBin ? L"\\assets\\bin\\" : L"\\") + Widen(BridgeFileName);
    return out;
}
std::wstring StableBridgePath(const std::wstring& browserExe, const std::wstring& installRoot) {
    return LocateBridge(browserExe, BridgeLayout::Sibling, installRoot).path;
}

bool BridgeUsable(const std::wstring& path, std::string* why) {
    auto no = [&](const char* reason) { if (why) *why = reason; return false; };
    const char* missing = "Le relais des annotations (browser-annotations-mcp.exe) est introuvable : rien n'a été modifié. "
                          "Réessayez après la mise à jour du navigateur, ou réinstallez-le.";
    const bool drive = path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
                       path[1] == L':' && path[2] == L'\\';
    if (!drive || path.find(L"\\..\\") != std::wstring::npos || path.find(L'/') != std::wstring::npos) return no(missing);
    const DWORD dirAttr = GetFileAttributesW(Parent(path).c_str());
    if (dirAttr == INVALID_FILE_ATTRIBUTES || !(dirAttr & FILE_ATTRIBUTE_DIRECTORY)) return no(missing);
    if (dirAttr & FILE_ATTRIBUTE_REPARSE_POINT) return no("Le dossier du relais des annotations est un lien : refusé, rien n'a été modifié.");
    // Opened WITHOUT following a reparse point: the attributes below are the file's own.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return no(missing);
    BY_HANDLE_FILE_INFORMATION info{};
    const bool regular = GetFileType(h) == FILE_TYPE_DISK && GetFileInformationByHandle(h, &info) &&
                         !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE));
    unsigned char head[4096] = {};
    DWORD got = 0;
    const bool read = regular && ReadFile(h, head, sizeof head, &got, nullptr);
    CloseHandle(h);
    if (!regular) return no("Le relais des annotations n'est pas un fichier ordinaire : refusé, rien n'a été modifié.");
    const char* notPe = "Le relais des annotations n'est pas un programme valide pour cet ordinateur : rien n'a été modifié. Réinstallez le navigateur.";
    if (!read || got < 0x40 || head[0] != 'M' || head[1] != 'Z') return no(notPe);
    const std::uint32_t pe = std::uint32_t(head[0x3C]) | (std::uint32_t(head[0x3D]) << 8) | (std::uint32_t(head[0x3E]) << 16) |
                             (std::uint32_t(head[0x3F]) << 24);
    // PE signature (4) + file header (20), then the optional header: magic at +0, subsystem at +68.
    if (pe < 0x40 || pe + 24 + 70 > got) return no(notPe);
    const unsigned char* p = head + pe;
    const unsigned machine = p[4] | (p[5] << 8), magic = p[24] | (p[25] << 8), subsystem = p[24 + 68] | (p[24 + 69] << 8);
    if (p[0] != 'P' || p[1] != 'E' || p[2] || p[3] || machine != 0x8664 || magic != 0x20B || subsystem != 3) return no(notPe);
    return true;
}

bool IsOnRemovableDrive(const Environment& env, const std::wstring& path) {
    const std::wstring full = FullPath(path);
    if (full.size() < 3 || full[1] != L':') return false;  // UNC and relative paths: not a drive letter
    const std::wstring root = full.substr(0, 2) + L"\\";
    const unsigned type = env.driveType ? env.driveType(root) : unsigned(GetDriveTypeW(root.c_str()));
    return type == DRIVE_REMOVABLE;
}

// ================================================================ snippets
std::string ClaudeJsonSnippet(const Target& t) {
    Value servers = Value::Obj();
    servers.Set(t.serverName, ServerValue(t));
    Value doc = Value::Obj();
    doc.Set("mcpServers", std::move(servers));
    return Pretty(doc) + "\n";
}
std::string CodexTomlSnippet(const Target& t) { return TomlBlock(t, "\n"); }
std::wstring CodexAddCommandLine(const Target& t) {
    return L"codex mcp add " + Widen(t.serverName) + L" -- " + QuoteArg(t.bridgeExe) + L" --store " + QuoteArg(t.storeRoot);
}

const char* ResultName(Result r) {
    switch (r) {
    case Result::Connected: return "connected";
    case Result::AlreadyConnected: return "already_connected";
    case Result::PendingInstall: return "pending_install";
    case Result::Disconnected: return "disconnected";
    case Result::NotConnected: return "not_connected";
    case Result::ManualStep: return "manual_step";
    case Result::NoClient: return "no_client";
    case Result::InvalidConfig: return "invalid_config";
    case Result::Failed: return "failed";
    }
    return "failed";
}

// ================================================================ zip
std::uint32_t Crc32(const std::string& data) {
    static const auto table = [] {
        std::uint32_t t[256];
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return std::vector<std::uint32_t>(t, t + 256);
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string ZipStored(const std::vector<ZipEntry>& entries, std::int64_t unixTime) {
    auto put16 = [](std::string& s, unsigned v) {
        s += char(v & 0xFF);
        s += char((v >> 8) & 0xFF);
    };
    auto put32 = [](std::string& s, std::uint32_t v) {
        for (int k = 0; k < 4; ++k) s += char((v >> (8 * k)) & 0xFF);
    };
    const Utc u = UtcOf(unixTime > 315532800 ? unixTime : 315532800);  // DOS dates start in 1980
    const unsigned dosTime = unsigned(u.hour << 11 | u.minute << 5 | u.second / 2);
    const unsigned dosDate = unsigned((u.year - 1980) << 9 | u.month << 5 | u.day);
    std::string out, central;
    for (const auto& e : entries) {
        const std::uint32_t crc = Crc32(e.data), size = std::uint32_t(e.data.size()), offset = std::uint32_t(out.size());
        put32(out, 0x04034b50);
        put16(out, 10);        // version needed: stored
        put16(out, 0x0800);    // UTF-8 names
        put16(out, 0);         // method: stored
        put16(out, dosTime);
        put16(out, dosDate);
        put32(out, crc);
        put32(out, size);
        put32(out, size);
        put16(out, unsigned(e.name.size()));
        put16(out, 0);
        out += e.name;
        out += e.data;
        put32(central, 0x02014b50);
        put16(central, 20);    // made by: MS-DOS / 2.0
        put16(central, 10);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, dosTime);
        put16(central, dosDate);
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, unsigned(e.name.size()));
        put16(central, 0);     // extra
        put16(central, 0);     // comment
        put16(central, 0);     // disk
        put16(central, 0);     // internal attributes
        put32(central, 0);     // external attributes
        put32(central, offset);
        central += e.name;
    }
    const std::uint32_t centralOffset = std::uint32_t(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, unsigned(entries.size()));
    put16(out, unsigned(entries.size()));
    put32(out, std::uint32_t(central.size()));
    put32(out, centralOffset);
    put16(out, 0);
    return out;
}

std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
        } else {
            out.append(backslashes, L'\\');
            out += arg[i];
        }
    }
    return out + L"\"";
}

// ================================================================ Claude Desktop
std::string McpbManifest(const Target& t) {
    Value author = Value::Obj();
    author.Set("name", Value::Str("browser"));
    Value args = Value::Arr();
    args.Push(Value::Str("--store"));
    args.Push(Value::Str(Narrow(t.storeRoot)));
    Value config = Value::Obj();
    // MANIFEST.md: "For binaries, apps will automatically append .exe on Windows" => no extension here.
    config.Set("command", Value::Str("${__dirname}/server/browser-annotations-mcp"));
    config.Set("args", std::move(args));
    config.Set("env", Value::Obj());
    Value server = Value::Obj();
    server.Set("type", Value::Str("binary"));
    server.Set("entry_point", Value::Str(std::string("server/") + BridgeFileName));
    server.Set("mcp_config", std::move(config));
    Value tools = Value::Arr();
    const char* list[][2] = {
        {"browser_annotations_list", "Lister les annotations ajoutées dans le navigateur, tous sites ou un site."},
        {"browser_annotation_get", "Lire une annotation : note, page, élément, styles et capture."},
        {"browser_annotation_claim", "Réserver une annotation avant de la traiter."},
        {"browser_annotation_resolve", "Clore une annotation avec un résumé."},
        {"browser_annotation_release", "Libérer une annotation réservée."},
    };
    for (const auto& tool : list) {
        Value v = Value::Obj();
        v.Set("name", Value::Str(tool[0]));
        v.Set("description", Value::Str(tool[1]));
        tools.Push(std::move(v));
    }
    Value platforms = Value::Arr();
    platforms.Push(Value::Str("win32"));
    Value compatibility = Value::Obj();
    compatibility.Set("platforms", std::move(platforms));
    Value m = Value::Obj();
    m.Set("manifest_version", Value::Str(McpbManifestVersion));
    m.Set("name", Value::Str(t.serverName));
    m.Set("display_name", Value::Str("Annotations du navigateur"));
    m.Set("version", Value::Str(t.version.empty() ? "0.0.0" : t.version));
    m.Set("description", Value::Str("Donne à Claude les annotations que vous ajoutez sur les pages web (note, élément, capture). "
                                     "Tout reste sur cet ordinateur."));
    m.Set("author", std::move(author));
    m.Set("server", std::move(server));
    m.Set("tools", std::move(tools));
    m.Set("compatibility", std::move(compatibility));
    return Pretty(m) + "\n";
}

bool BuildMcpb(const Target& t, const std::wstring& outPath, std::string* error) {
    std::string exe;
    if (ReadAll(t.bridgeExe, 64ull << 20, exe) != Read::Ok || exe.size() < 2 || exe.compare(0, 2, "MZ") != 0) {
        if (error) *error = "bridge unreadable: " + Narrow(t.bridgeExe);
        return false;
    }
    const std::int64_t now = BA::SystemNow();
    const std::string zip = ZipStored({{"manifest.json", McpbManifest(t)}, {std::string("server/") + BridgeFileName, exe}}, now);
    if (!AtomicWrite(outPath, zip)) {
        if (error) *error = "cannot write " + Narrow(outPath);
        return false;
    }
    return true;
}

ConfiguredEntry ReadClaudeEntry(const std::wstring& configPath, const std::string& serverName) {
    std::string text;
    if (configPath.empty() || ReadAll(configPath, MaxConfigBytes, text) != Read::Ok) return {};
    Value v;
    if (!SJ::Parse(text, v, nullptr, ConfigLimits()) || !v.IsObject()) return {};
    const Value* servers = v.Get("mcpServers");
    if (!servers || !servers->IsObject()) return {};
    return EntryFromJson(servers->Get(serverName));
}

bool ClaudeExtensionInstalled(const Environment& env, const std::string& serverName) { return InstalledExtension(env, serverName, nullptr); }

Report MergeClaudeConfig(const std::wstring& configPath, const Target& t, std::int64_t now) {
    std::string text;
    const Read r = ReadAll(configPath, MaxConfigBytes, text);
    if (r == Read::TooBig) return Make(Result::InvalidConfig, "Configuration de Claude trop grande : réglage manuel nécessaire.", configPath);
    if (r == Read::Error) return Make(Result::Failed, "Configuration de Claude illisible.", configPath);
    std::size_t bodyStart = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    const bool blank = SkipWs(text, bodyStart) >= text.size();
    std::string out;
    if (blank) {
        out = text.substr(0, bodyStart) + ClaudeJsonSnippet(t);
    } else {
        Value doc;
        std::string why;
        if (!SJ::Parse(text, doc, &why, ConfigLimits()) || !doc.IsObject())
            return Make(Result::InvalidConfig, "Le fichier de configuration de Claude n'est pas un JSON valide : rien n'a été modifié.", configPath);
        const Value* servers = doc.Get("mcpServers");
        if (servers && !servers->IsObject())
            return Make(Result::InvalidConfig, "« mcpServers » n'est pas un objet dans la configuration de Claude : rien n'a été modifié.", configPath);
        if (servers && EntryMatches(EntryFromJson(servers->Get(t.serverName)), t))
            return Make(Result::AlreadyConnected, "Claude est déjà configuré.", configPath);
        const std::string nl = NewlineOf(text);
        const std::size_t rootOpen = SkipWs(text, bodyStart);
        std::size_t rootClose = 0;
        const std::vector<Member> root = Members(text, rootOpen, rootClose);
        std::string unit = "  ", rootIndent;
        const bool pretty = root.empty() ? text.find('\n', rootOpen) < rootClose : LineIndent(text, root.front().keyStart, rootIndent);
        if (pretty && !rootIndent.empty()) unit = rootIndent;
        const std::string name = SJ::Serialize(Value::Str(t.serverName));
        auto valueText = [&](const std::string& base) { return pretty ? Pretty(ServerValue(t), base, unit, nl) : SJ::Serialize(ServerValue(t)); };
        out = text;
        const auto it = std::find_if(root.begin(), root.end(), [](const Member& m) { return m.key == "mcpServers"; });
        if (it == root.end()) {
            Value servers2 = Value::Obj();
            servers2.Set(t.serverName, ServerValue(t));
            const std::string member = "\"mcpServers\": " + (pretty ? Pretty(servers2, unit, unit, nl) : SJ::Serialize(servers2));
            if (root.empty()) out.replace(rootOpen + 1, rootClose - rootOpen - 1, pretty ? nl + unit + member + nl : member);
            else out.insert(root.back().valueEnd, "," + (pretty ? nl + unit : std::string()) + member);
        } else {
            std::size_t close = 0;
            const std::vector<Member> list = Members(text, it->valueStart, close);
            std::string serversIndent;
            const bool serversOwnLine = LineIndent(text, it->keyStart, serversIndent);
            const auto ours = std::find_if(list.begin(), list.end(), [&](const Member& m) { return m.key == t.serverName; });
            if (ours != list.end()) {
                std::string indent;
                const bool own = LineIndent(text, ours->keyStart, indent);
                out.replace(ours->valueStart, ours->valueEnd - ours->valueStart, own ? Pretty(ServerValue(t), indent, unit, nl) : SJ::Serialize(ServerValue(t)));
            } else if (list.empty()) {
                const std::string inner = serversIndent + unit;
                out.replace(it->valueStart + 1, close - it->valueStart - 1,
                            pretty && serversOwnLine ? nl + inner + name + ": " + valueText(inner) + nl + serversIndent : name + ":" + SJ::Serialize(ServerValue(t)));
            } else {
                std::string indent;
                const bool memberPretty = LineIndent(text, list.front().keyStart, indent);
                out.insert(list.back().valueEnd, "," + (memberPretty ? nl + indent : std::string()) + name + ": " +
                                                     (memberPretty ? Pretty(ServerValue(t), indent, unit, nl) : SJ::Serialize(ServerValue(t))));
            }
        }
    }
    // Safety net: the result must parse strictly and carry exactly our entry.
    Value check;
    if (!SJ::Parse(out, check, nullptr, ConfigLimits()) || !check.IsObject() || !check.Get("mcpServers") ||
        !EntryMatches(EntryFromJson(check.Get("mcpServers")->Get(t.serverName)), t))
        return Make(Result::Failed, "Fusion de la configuration de Claude impossible : rien n'a été modifié.", configPath);
    std::wstring backup;
    const bool backupOwned = true;
    if (!Backup(configPath, now, backup)) return Make(Result::Failed, "Sauvegarde de la configuration de Claude impossible : rien n'a été modifié.", configPath);
    if (!AtomicWrite(configPath, out)) {
        if (backupOwned) DropBackup(backup);
        return Make(Result::Failed, "Écriture de la configuration de Claude impossible : rien n'a été modifié.", configPath, backup);
    }
    return Make(Result::Connected, "Claude est configuré. Redémarrez Claude pour l'activer.", configPath, backup);
}

Report RemoveClaudeConfig(const std::wstring& configPath, const std::string& serverName, std::int64_t now) {
    std::string text;
    const Read r = ReadAll(configPath, MaxConfigBytes, text);
    if (r == Read::Missing) return Make(Result::NotConnected, "Claude n'était pas configuré.", configPath);
    if (r != Read::Ok) return Make(Result::Failed, "Configuration de Claude illisible.", configPath);
    const std::size_t bodyStart = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    if (SkipWs(text, bodyStart) >= text.size()) return Make(Result::NotConnected, "Claude n'était pas configuré.", configPath);
    Value doc;
    if (!SJ::Parse(text, doc, nullptr, ConfigLimits()) || !doc.IsObject())
        return Make(Result::InvalidConfig, "Le fichier de configuration de Claude n'est pas un JSON valide : rien n'a été modifié.", configPath);
    const Value* servers = doc.Get("mcpServers");
    if (!servers || !servers->IsObject() || !servers->Get(serverName)) return Make(Result::NotConnected, "Claude n'était pas configuré.", configPath);
    std::size_t rootClose = 0;
    const std::vector<Member> root = Members(text, SkipWs(text, bodyStart), rootClose);
    const auto it = std::find_if(root.begin(), root.end(), [](const Member& m) { return m.key == "mcpServers"; });
    std::size_t close = 0;
    const std::vector<Member> list = Members(text, it->valueStart, close);
    const auto ours = std::find_if(list.begin(), list.end(), [&](const Member& m) { return m.key == serverName; });
    std::string out = text;
    if (list.size() == 1) {
        out.erase(it->valueStart + 1, close - it->valueStart - 1);  // "mcpServers": {}
    } else if (ours != list.begin()) {
        out.erase((ours - 1)->valueEnd, ours->valueEnd - (ours - 1)->valueEnd);
    } else {
        out.erase(ours->keyStart, (ours + 1)->keyStart - ours->keyStart);
    }
    Value check;
    if (!SJ::Parse(out, check, nullptr, ConfigLimits()) || !check.IsObject() || check.Get("mcpServers")->Get(serverName))
        return Make(Result::Failed, "Retrait impossible : rien n'a été modifié.", configPath);
    std::wstring backup;
    const bool backupOwned = true;
    if (!Backup(configPath, now, backup)) return Make(Result::Failed, "Sauvegarde de la configuration de Claude impossible : rien n'a été modifié.", configPath);
    if (!AtomicWrite(configPath, out)) {
        if (backupOwned) DropBackup(backup);
        return Make(Result::Failed, "Écriture de la configuration de Claude impossible : rien n'a été modifié.", configPath, backup);
    }
    return Make(Result::Disconnected, "Claude est déconnecté. Redémarrez Claude pour l'appliquer.", configPath, backup);
}

bool WriteAllowed(const Environment& env, const std::wstring& path, std::string* why) {
    if (!env.testMode) return true;
    const std::wstring target = LowerW(StripSlash(FullPath(path)));
    std::vector<std::wstring> roots = env.protectedRoots ? env.protectedRoots() : std::vector<std::wstring>{};
    if (roots.empty()) { if (why) *why = "Mode test : dossiers réels inconnus, écriture refusée."; return false; }
    for (const auto& r : roots) {
        const std::wstring root = LowerW(StripSlash(FullPath(r)));
        if (root.empty()) continue;
        if (target == root || (target.size() > root.size() && target.compare(0, root.size(), root) == 0 && target[root.size()] == L'\\')) {
            if (why) *why = "Mode test (BROWSER_ANNOTATIONS_TEST ou débogage distant) : écriture refusée dans la vraie configuration de l'utilisateur. Redirigez BROWSER_ANNOTATIONS_CONNECT_ROOT.";
            return false;
        }
    }
    return true;
}
namespace {
Report Refused(const std::string& why, const std::wstring& file) { return Make(Result::Failed, why, file); }
}

Report ConnectClaude(const Environment& env, const Target& t, const std::wstring& workDir) {
    const std::wstring config = ClaudeConfigPath(env);
    if (config.empty()) return Make(Result::NoClient, "Claude Desktop n'est pas installé sur cet ordinateur (aucun dossier de configuration).");
    if (std::string why; !WriteAllowed(env, config, &why)) return Refused(why, config);
    if (std::string why; !BridgeUsable(t.bridgeExe, &why)) return Refused(why, t.bridgeExe);   // before any write: never a config to a missing bridge
    ConfiguredEntry ext;
    if (InstalledExtension(env, t.serverName, &ext) && ext.args.size() == 2 && ext.args[0] == L"--store" && SamePath(ext.args[1], t.storeRoot))
        return Make(Result::AlreadyConnected, "L'extension est déjà installée dans Claude.");
    if (EntryMatches(ReadClaudeEntry(config, t.serverName), t)) return Make(Result::AlreadyConnected, "Claude est déjà configuré.", config);
    std::string why;
    if (!env.testMode && env.mcpbAssociated && env.mcpbAssociated() && !workDir.empty()) {   // test mode: never Claude's installer
        const std::wstring bundle = StripSlash(workDir) + L"\\" + Widen(t.serverName) + L".mcpb";
        if (BuildMcpb(t, bundle, &why) && env.openFile && env.openFile(bundle)) {
            Report rep = Make(Result::PendingInstall, "Cliquez sur Installer dans Claude.", bundle);
            return rep;
        }
    }
    Report rep = MergeClaudeConfig(config, t, env.now ? env.now() : BA::SystemNow());
    return rep;
}

Report DisconnectClaude(const Environment& env, const Target& t) {
    const std::wstring config = ClaudeConfigPath(env);
    if (config.empty()) return Make(Result::NoClient, "Claude Desktop n'est pas installé sur cet ordinateur.");
    if (std::string why; !WriteAllowed(env, config, &why)) return Refused(why, config);
    Report rep = RemoveClaudeConfig(config, t.serverName, env.now ? env.now() : BA::SystemNow());
    if (rep.ok() && ClaudeExtensionInstalled(env, t.serverName)) {
        rep.result = Result::ManualStep;
        rep.message = "Désinstallez « Annotations du navigateur » dans Claude › Réglages › Extensions.";
    }
    return rep;
}

// ================================================================ Codex
ConfiguredEntry ReadCodexEntry(const std::wstring& configPath, const std::string& serverName) {
    std::string text;
    if (configPath.empty() || ReadAll(configPath, MaxConfigBytes, text) != Read::Ok) return {};
    const TomlScan scan = ScanToml(text);
    if (!scan.ok) return {};
    return EntryFromToml(text, scan, serverName);
}

namespace {
Report MergeToml(const std::wstring& configPath, const Target& t, std::int64_t now, bool backupFirst, const std::wstring& existingBackup) {
    std::string text;
    const Read r = ReadAll(configPath, MaxConfigBytes, text);
    if (r == Read::TooBig) return Make(Result::InvalidConfig, "Configuration de Codex trop grande : réglage manuel nécessaire.", configPath);
    if (r == Read::Error) return Make(Result::Failed, "Configuration de Codex illisible.", configPath);
    const TomlScan scan = ScanToml(text);
    const TomlPlan plan = PlanToml(text, scan, t.serverName);
    if (!plan.scanned) return Make(Result::InvalidConfig, "Le fichier config.toml de Codex n'a pas pu être lu : rien n'a été modifié.", configPath);
    if (plan.foreign)
        return Make(Result::InvalidConfig, "Codex a déjà une entrée « " + t.serverName + " » écrite autrement : réglage manuel nécessaire.", configPath);
    if (EntryMatches(EntryFromToml(text, scan, t.serverName), t)) return Make(Result::AlreadyConnected, "Codex est déjà configuré.", configPath);
    const std::string nl = NewlineOf(text);
    std::string out = text;
    const std::string block = TomlBlock(t, nl);
    std::size_t at = std::string::npos;
    if (!plan.blocks.empty()) {  // replace in place (our blocks removed, the new one where the first was)
        at = plan.blocks.front().first;
        for (auto b = plan.blocks.rbegin(); b != plan.blocks.rend(); ++b) EraseBlock(out, b->first, b->second, nl);
    }
    if (at < out.size()) {
        out.insert(at, block + (out.compare(at, nl.size(), nl) != 0 ? nl : std::string()));
    } else {
        if (!out.empty() && !EndsWith(out, "\n")) out += nl;
        const std::size_t bom = out.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
        if (out.size() > bom && !EndsWith(out, nl + nl)) out += nl;
        out += block;
    }
    const TomlScan after = ScanToml(out);
    if (!after.ok || PlanToml(out, after, t.serverName).blocks.size() != 1 || !EntryMatches(EntryFromToml(out, after, t.serverName), t))
        return Make(Result::Failed, "Modification de config.toml impossible : rien n'a été modifié.", configPath);
    std::wstring backup = existingBackup;
    const bool backupOwned = backupFirst;  // a backup handed in by ConnectCodex/DisconnectCodex predates the CLI run: kept
    if (backupFirst && !Backup(configPath, now, backup)) return Make(Result::Failed, "Sauvegarde de config.toml impossible : rien n'a été modifié.", configPath);
    if (!AtomicWrite(configPath, out)) {
        if (backupOwned) DropBackup(backup);
        return Make(Result::Failed, "Écriture de config.toml impossible : rien n'a été modifié.", configPath, backup);
    }
    return Make(Result::Connected, "Codex est configuré. Redémarrez Codex pour l'activer.", configPath, backup);
}

Report RemoveToml(const std::wstring& configPath, const std::string& serverName, std::int64_t now, bool backupFirst, const std::wstring& existingBackup) {
    std::string text;
    const Read r = ReadAll(configPath, MaxConfigBytes, text);
    if (r == Read::Missing) return Make(Result::NotConnected, "Codex n'était pas configuré.", configPath);
    if (r != Read::Ok) return Make(Result::Failed, "Configuration de Codex illisible.", configPath);
    const TomlScan scan = ScanToml(text);
    const TomlPlan plan = PlanToml(text, scan, serverName);
    if (!plan.scanned) return Make(Result::InvalidConfig, "Le fichier config.toml de Codex n'a pas pu être lu : rien n'a été modifié.", configPath);
    if (plan.blocks.empty()) {
        if (plan.foreign)
            return Make(Result::InvalidConfig, "L'entrée « " + serverName + " » de Codex est écrite autrement : retirez-la à la main.", configPath);
        return Make(Result::NotConnected, "Codex n'était pas configuré.", configPath);
    }
    const std::string nl = NewlineOf(text);
    std::string out = text;
    for (auto b = plan.blocks.rbegin(); b != plan.blocks.rend(); ++b) EraseBlock(out, b->first, b->second, nl);
    const TomlScan after = ScanToml(out);
    if (!after.ok || !PlanToml(out, after, serverName).blocks.empty())
        return Make(Result::Failed, "Retrait impossible : rien n'a été modifié.", configPath);
    std::wstring backup = existingBackup;
    const bool backupOwned = backupFirst;  // a backup handed in by ConnectCodex/DisconnectCodex predates the CLI run: kept
    if (backupFirst && !Backup(configPath, now, backup)) return Make(Result::Failed, "Sauvegarde de config.toml impossible : rien n'a été modifié.", configPath);
    if (!AtomicWrite(configPath, out)) {
        if (backupOwned) DropBackup(backup);
        return Make(Result::Failed, "Écriture de config.toml impossible : rien n'a été modifié.", configPath, backup);
    }
    return Make(Result::Disconnected, "Codex est déconnecté. Redémarrez Codex pour l'appliquer.", configPath, backup);
}
}  // namespace

Report MergeCodexToml(const std::wstring& configPath, const Target& t, std::int64_t now) { return MergeToml(configPath, t, now, true, L""); }
Report RemoveCodexToml(const std::wstring& configPath, const std::string& serverName, std::int64_t now) {
    return RemoveToml(configPath, serverName, now, true, L"");
}

Report ConnectCodex(const Environment& env, const Target& t) {
    const std::wstring home = CodexHome(env);
    if (home.empty() || !DirExists(home)) return Make(Result::NoClient, "Codex n'est pas installé sur cet ordinateur (aucun dossier de configuration).");
    const std::wstring config = home + L"\\config.toml";
    if (std::string why; !WriteAllowed(env, home, &why)) return Refused(why, config);
    if (std::string why; !BridgeUsable(t.bridgeExe, &why)) return Refused(why, t.bridgeExe);   // before the CLI or any write
    const std::int64_t now = env.now ? env.now() : BA::SystemNow();
    // Pre-flight on the current file: same refusals whichever way we write.
    std::string text;
    const Read r = ReadAll(config, MaxConfigBytes, text);
    if (r == Read::TooBig || r == Read::Error) return Make(r == Read::TooBig ? Result::InvalidConfig : Result::Failed, "Configuration de Codex illisible.", config);
    const TomlScan scan = ScanToml(text);
    const TomlPlan plan = PlanToml(text, scan, t.serverName);
    if (!plan.scanned) return Make(Result::InvalidConfig, "Le fichier config.toml de Codex n'a pas pu être lu : rien n'a été modifié.", config);
    if (plan.foreign)
        return Make(Result::InvalidConfig, "Codex a déjà une entrée « " + t.serverName + " » écrite autrement : réglage manuel nécessaire.", config);
    if (EntryMatches(EntryFromToml(text, scan, t.serverName), t)) return Make(Result::AlreadyConnected, "Codex est déjà configuré.", config);
    std::wstring backup;
    if (!Backup(config, now, backup)) return Make(Result::Failed, "Sauvegarde de config.toml impossible : rien n'a été modifié.", config);
    const std::wstring cli = FindCodexCli(env);
    bool usedCli = false;
    if (!cli.empty()) {
        usedCli = true;
        const int code = RunHidden(cli, {L"mcp", L"add", Widen(t.serverName), L"--", t.bridgeExe, L"--store", t.storeRoot}, home, home, CliTimeoutMs);
        if (code == 0 && EntryMatches(ReadCodexEntry(config, t.serverName), t)) {
            Report rep = Make(Result::Connected, "Codex est configuré. Redémarrez Codex pour l'activer.", config, backup);
            rep.usedCli = true;
            return rep;
        }
    }
    Report rep = MergeToml(config, t, now, false, backup);  // the backup above holds the original file
    rep.usedCli = usedCli;
    return rep;
}

Report DisconnectCodex(const Environment& env, const Target& t) {
    const std::wstring home = CodexHome(env);
    if (home.empty() || !DirExists(home)) return Make(Result::NoClient, "Codex n'est pas installé sur cet ordinateur.");
    const std::wstring config = home + L"\\config.toml";
    if (std::string why; !WriteAllowed(env, home, &why)) return Refused(why, config);
    const std::int64_t now = env.now ? env.now() : BA::SystemNow();
    if (!ReadCodexEntry(config, t.serverName).present) {
        Report probe = RemoveToml(config, t.serverName, now, false, L"");  // reports InvalidConfig / NotConnected, writes nothing
        if (probe.result != Result::Disconnected) return probe;
    }
    std::wstring backup;
    if (!Backup(config, now, backup)) return Make(Result::Failed, "Sauvegarde de config.toml impossible : rien n'a été modifié.", config);
    const std::wstring cli = FindCodexCli(env);
    if (!cli.empty()) {
        const int code = RunHidden(cli, {L"mcp", L"remove", Widen(t.serverName)}, home, home, CliTimeoutMs);
        if (code == 0 && !ReadCodexEntry(config, t.serverName).present) {
            Report rep = Make(Result::Disconnected, "Codex est déconnecté. Redémarrez Codex pour l'appliquer.", config, backup);
            rep.usedCli = true;
            return rep;
        }
    }
    Report rep = RemoveToml(config, t.serverName, now, false, backup);
    rep.usedCli = !cli.empty();
    return rep;
}

// ================================================================ state
Status ClientStatus(const Environment& env, Client c, const Target& t, const std::vector<BA::Reader>& readers, std::int64_t recentSeconds) {
    Status s;
    ConfiguredEntry entry;
    if (c == Client::Claude) {
        entry = ReadClaudeEntry(ClaudeConfigPath(env), t.serverName);
        ConfiguredEntry ext;
        s.extensionInstalled = InstalledExtension(env, t.serverName, &ext);
        if (s.extensionInstalled && !(ext.args.size() == 2 && ext.args[0] == L"--store" && SamePath(ext.args[1], t.storeRoot))) s.stalePath = true;
    } else {
        entry = ReadCodexEntry(CodexConfigPath(env), t.serverName);
    }
    s.configured = entry.present || s.extensionInstalled;
    if (entry.present && !EntryMatches(entry, t)) s.stalePath = true;
    const std::int64_t now = env.now ? env.now() : BA::SystemNow();
    const char* needle = c == Client::Claude ? "claude" : "codex";
    for (const auto& r : readers) {
        if (r.client.find(needle) == std::string::npos || r.lastReadAt <= s.lastReadAt) continue;
        s.lastReadAt = r.lastReadAt;
        s.readerLabel = r.label;
    }
    s.recentlyRead = s.lastReadAt > 0 && now - s.lastReadAt <= recentSeconds;
    s.removableDrive = IsOnRemovableDrive(env, t.bridgeExe) || IsOnRemovableDrive(env, t.storeRoot);
    return s;
}

}  // namespace AgentConnect
