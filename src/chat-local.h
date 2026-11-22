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

} // namespace ChatLocal
