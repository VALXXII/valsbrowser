#pragma once
// Chat intégré — « sites locaux seulement » (design/annotations-chat-v1-brief.md, décision 2).
// Pure C++17 (no Win32): the one rule that decides whether a page may talk to an agent that writes code.
// Local = http(s) on localhost, *.localhost, 127.0.0.0/8, [::1], or a file: URL. Everything else is refused,
// with an explanation in the panel (state.disabledReason). The check is on the URL the ENGINE reports for the
// tab (never a string the page gives), parsed strictly: userinfo, odd ports and look-alike hosts
// (localhost.example.com, 127.0.0.1.example.com, 0x7f.1, 2130706433) are not local.
#include <cstdint>
#include <string>
#include <string_view>

namespace ChatLocal {

struct Site {
    bool local = false;
    bool file = false;          // file: URL (no origin, no port)
    std::string scheme, host;   // lower case; host without brackets for IPv6
    int port = 0;               // explicit or the scheme's default (80 / 443); 0 for file:
    std::string origin;         // "http://localhost:5173" (default ports omitted), "file://" for files
    std::string label;          // "localhost:5173", "127.0.0.1:5173", "fichier local" is the caller's text
};

namespace detail {
inline char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
inline bool Digits(std::string_view s) { if (s.empty()) return false; for (char c : s) if (c < '0' || c > '9') return false; return true; }
// Strict dotted decimal a.b.c.d, each 0..255, no leading zero forms beyond "0" (no octal / hex / short forms).
inline bool Ipv4(std::string_view h, int out[4]) {
    int part = 0; std::size_t i = 0;
    while (part < 4) {
        std::size_t j = i; int v = 0;
        while (j < h.size() && h[j] >= '0' && h[j] <= '9') { v = v * 10 + (h[j] - '0'); if (v > 255) return false; ++j; }
        if (j == i || j - i > 3 || (j - i > 1 && h[i] == '0')) return false;
        out[part++] = v;
        if (part == 4) return j == h.size();
        if (j >= h.size() || h[j] != '.') return false;
        i = j + 1;
    }
    return false;
}
inline bool LoopbackHost(std::string_view host) {
    if (host == "localhost") return true;
    if (host.size() > 10 && host.substr(host.size() - 10) == ".localhost") {   // *.localhost (RFC 6761)
        for (char c : host) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
        return host.find("..") == std::string_view::npos && host[0] != '.' && host[0] != '-';
    }
    int ip[4]; return Ipv4(host, ip) && ip[0] == 127;
}
}

// `url`: the tab's address as the engine reports it.
inline Site Classify(std::string_view url) {
    Site s;
    const auto colon = url.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 8) return s;
    for (char c : url.substr(0, colon)) s.scheme += detail::Lower(c);
    if (s.scheme == "file") {
        if (url.substr(colon, 3) != "://") return s;
        std::string_view rest = url.substr(colon + 3);
        const auto slash = rest.find('/');
        const std::string_view host = slash == std::string_view::npos ? rest : rest.substr(0, slash);
        if (!host.empty() && host != "localhost") return s;   // file://server/share: a network share is not local
        s.file = s.local = true; s.origin = "file://"; return s;
    }
    if (s.scheme != "http" && s.scheme != "https") return s;
    if (url.substr(colon, 3) != "://") return s;
    std::string_view rest = url.substr(colon + 3);
    const auto end = rest.find_first_of("/?#");
    std::string_view authority = end == std::string_view::npos ? rest : rest.substr(0, end);
    if (authority.empty() || authority.find('@') != std::string_view::npos || authority.find('\\') != std::string_view::npos) return s;
    std::string_view host = authority, port;
    if (authority[0] == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) return s;
        host = authority.substr(1, close - 1);
        const std::string_view tail = authority.substr(close + 1);
        if (!tail.empty()) { if (tail[0] != ':') return s; port = tail.substr(1); if (!detail::Digits(port)) return s; }
    } else if (const auto pc = authority.rfind(':'); pc != std::string_view::npos) {
        host = authority.substr(0, pc); port = authority.substr(pc + 1);
        if (!detail::Digits(port)) return s;
    }
    const bool v6 = authority[0] == '[';
    if (host.empty() || host.size() > 253 || (!v6 && host.find(':') != std::string_view::npos)) return s;
    for (char c : host) s.host += detail::Lower(c);
    const int def = s.scheme == "https" ? 443 : 80;
    s.port = def;
    if (!port.empty()) { if (port.size() > 5) return s; s.port = 0; for (char c : port) s.port = s.port * 10 + (c - '0'); if (s.port < 1 || s.port > 65535) return s; }
    const std::string shown = v6 ? "[" + s.host + "]" : s.host;
    s.label = shown + (s.port == def ? "" : ":" + std::to_string(s.port));
    s.origin = s.scheme + "://" + s.label;
    s.local = v6 ? (s.host == "::1" || s.host == "0:0:0:0:0:0:0:1") : detail::LoopbackHost(s.host);
    return s;
}
inline bool IsLocal(std::string_view url) { return Classify(url).local; }

// Lot R (decision A, 2026-11-24): the chat also works on a local FILE. Its « site » is then the FOLDER that holds the
// file — its conversations, its remembered choices and its project folder are that folder's, never « every file of the
// disk ». Pure: the folder as the engine's URL names it (file:///C:/dir/page.html -> C:\dir). Refused (ok = false): a
// path that is not on a drive letter, a « . » / « .. » step, a backslash or a control character left after decoding.
struct FileFolder {
    bool ok = false;
    std::string folder;   // UTF-8 Windows path, no trailing backslash except a drive root: "C:\dir", "C:\"
    std::string origin;   // the site's key: "file:///c:/dir" (ASCII lower case: Windows paths ignore case)
    std::string label;    // the folder's own name ("dir"; "C:" for a drive root)
};
inline FileFolder FolderOfFile(std::string_view url) {
    FileFolder f;
    if (!Classify(url).file) return f;
    std::string_view rest = url.substr(url.find("://") + 3);
    const auto slash = rest.find('/'); if (slash == std::string_view::npos) return f;
    rest = rest.substr(slash);                                             // "/C:/dir/page.html?x#y"
    if (const auto cut = rest.find_first_of("?#"); cut != std::string_view::npos) rest = rest.substr(0, cut);
    std::string path;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        char c = rest[i];
        if (c == '%') {
            auto hex = [](char h) { return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1; };
            if (i + 2 >= rest.size() || hex(rest[i + 1]) < 0 || hex(rest[i + 2]) < 0) return f;
            c = char(hex(rest[i + 1]) * 16 + hex(rest[i + 2])); i += 2;
        }
        if (static_cast<unsigned char>(c) < 0x20 || c == '\\' || c == '"' || c == '<' || c == '>' || c == '|' || c == '*' || c == '?') return f;
        path += c;
    }
    // "/C:/…": a drive letter, then the folders, then the file's own name
    if (path.size() < 4 || path[0] != '/' || path[2] != ':' || path[3] != '/') return f;
    const char drive = path[1] >= 'a' && path[1] <= 'z' ? char(path[1] - 'a' + 'A') : path[1];
    if (drive < 'A' || drive > 'Z') return f;
    std::string dir = std::string(1, drive) + ":"; std::string last = dir;
    std::size_t at = 4;
    while (at <= path.size()) {
        const auto next = path.find('/', at);
        if (next == std::string::npos) break;                               // what is left is the file's name
        const std::string step = path.substr(at, next - at); at = next + 1;
        if (step.empty() || step == "." || step == ".." || step.find(':') != std::string::npos || step.back() == ' ' || step.back() == '.') return f;
        dir += "\\" + step; last = step;
    }
    f.folder = dir.size() == 2 ? dir + "\\" : dir; f.label = last;
    std::string key = dir; for (auto& c : key) { if (c == '\\') c = '/'; else c = detail::Lower(c); }
    if (key.size() > 200) {   // a very deep folder: a short stable key (FNV-1a 64) — an origin is stored in 300 bytes at most
        std::uint64_t h = 1469598103934665603ull; for (unsigned char c : key) { h ^= c; h *= 1099511628211ull; }
        static const char* digits = "0123456789abcdef"; std::string hex; for (int i = 15; i >= 0; --i) hex += digits[(h >> (i * 4)) & 15];
        key = "~" + hex;
    }
    f.origin = "file:///" + key; f.ok = true;
    return f;
}
// The chat's site for a page: a local project as Classify says it, or a local file's folder. A file whose folder
// cannot be read from its URL is not a site the chat works on (local = false).
inline Site SiteOf(std::string_view url) {
    Site s = Classify(url);
    if (!s.file) return s;
    const FileFolder f = FolderOfFile(url);
    if (!f.ok) { s.local = false; return s; }
    s.origin = f.origin; s.label = f.label;
    return s;
}

} // namespace ChatLocal
