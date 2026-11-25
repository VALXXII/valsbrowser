#pragma once
// Chat intégré — which tool an agent is asking for (design/chat-integration.md, « Autorisations lisibles »).
// Pure C++17 (no Win32, no I/O, no interface text): the integration turns these facts into sentences of the catalog.
//
// An agent names an MCP tool in the title of its tool call (checked in the adapters' own code, Nov. 2026):
//   Claude Code   mcp__<server>__<tool>   the server is the segment up to the next « __ », and it is the NORMALISED
//                                         name (every character outside [A-Za-z0-9_-] replaced by « _ »)
//   Codex         mcp.<server>.<tool>     the server's name as configured
// Our own MCP server (the annotation bridge handed to the session, src/mcp-bridge.cpp) has five tools. They read and
// write the annotation store only — never the project's code, never a command, never the network — which is why the
// integration answers their permission requests itself (allow once, no card), even in « Demander ».
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "strict-json.h"

namespace ChatTools {

// A request the agent names by a fixed English title (codex-acp: « Additional sandbox permissions »): the same title
// whatever its case and the spaces around it — nothing longer, nothing that only contains it.
inline bool SameTitle(std::string_view title, std::string_view known) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!title.empty() && blank(title.front())) title.remove_prefix(1);
    while (!title.empty() && blank(title.back())) title.remove_suffix(1);
    if (title.size() != known.size()) return false;
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
    for (std::size_t i = 0; i < title.size(); ++i) if (lower(title[i]) != lower(known[i])) return false;
    return true;
}
struct McpName {
    bool mcp = false;
    std::string server, tool;   // as written in the title
};
inline McpName ParseMcpName(std::string_view title) {
    McpName n;
    auto ok = [](std::string_view s) {
        if (s.empty() || s.size() > 128) return false;
        for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ' ')) return false;
        return true; };
    if (title.rfind("mcp__", 0) == 0) {
        const std::string_view rest = title.substr(5); const auto sep = rest.find("__");
        if (sep == std::string_view::npos || sep == 0 || sep + 2 >= rest.size()) return n;
        const std::string_view server = rest.substr(0, sep), tool = rest.substr(sep + 2);
        if (!ok(server) || !ok(tool) || server.find('.') != std::string_view::npos) return n;
        n.mcp = true; n.server = std::string(server); n.tool = std::string(tool);
    } else if (title.rfind("mcp.", 0) == 0) {
        const std::string_view rest = title.substr(4); const auto sep = rest.rfind('.');   // tool names have no dot; a server's may
        if (sep == std::string_view::npos || sep == 0 || sep + 1 >= rest.size()) return n;
        const std::string_view server = rest.substr(0, sep), tool = rest.substr(sep + 1);
        if (!ok(server) || !ok(tool)) return n;
        n.mcp = true; n.server = std::string(server); n.tool = std::string(tool);
    }
    return n;
}
// A server's configured name as Claude Code writes it inside a tool name.
inline std::string NormalizeServer(std::string_view name) {
    std::string out; for (char c : name) out += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') ? c : '_';
    return out;
}

enum class Ours { None, List, Get, Claim, Resolve, Release };
// One of OUR annotation tools, on OUR server (the exact name given to the session), or None. Anything else — another
// server, an unknown tool of ours, a look-alike name — is None and goes through the ordinary permission card.
inline Ours OurTool(const McpName& n, std::string_view ourServer) {
    if (!n.mcp || ourServer.empty() || (n.server != ourServer && n.server != NormalizeServer(ourServer))) return Ours::None;
    if (n.tool == "browser_annotations_list") return Ours::List;
    if (n.tool == "browser_annotation_get") return Ours::Get;
    if (n.tool == "browser_annotation_claim") return Ours::Claim;
    if (n.tool == "browser_annotation_resolve") return Ours::Resolve;
    if (n.tool == "browser_annotation_release") return Ours::Release;
    return Ours::None;
}
// SEC-28 (audit 2026-11-23): a request is answered without a card only when, besides its title, it also looks like one of
// our tools: not a shell command, an edit, a deletion, a move or a fetch, and its arguments limited to that tool's own
// keys (the bridge's CheckArgs, src/mcp-bridge.cpp). Anything else goes through the ordinary permission card.
inline bool LooksLikeOurs(Ours tool, std::string_view kind, std::string_view rawInputJson) {
    if (tool == Ours::None) return false;
    if (kind == "execute" || kind == "edit" || kind == "delete" || kind == "move" || kind == "fetch") return false;
    std::vector<std::string_view> keys;
    switch (tool) {
    case Ours::List: keys = {"site", "status", "limit", "cursor"}; break;
    case Ours::Get: keys = {"id", "include_screenshot"}; break;
    case Ours::Claim: keys = {"id", "agent_label", "claim_token"}; break;
    case Ours::Resolve: keys = {"id", "claim_token", "outcome", "summary"}; break;
    case Ours::Release: keys = {"id", "claim_token", "reason"}; break;
    default: return false;
    }
    if (rawInputJson.empty()) return true;   // the arguments are not known yet: the title and the kind decide
    StrictJson::Limits lim; lim.maxBytes = 64 * 1024; lim.maxDepth = 8;
    StrictJson::Value v; if (!StrictJson::Parse(rawInputJson, v, nullptr, lim) || !v.IsObject()) return false;
    for (const auto& m : v.members) { bool known = false; for (const auto k : keys) if (m.first == k) known = true; if (!known) return false; }
    return true;
}
// The annotation a call is about: the « id » argument when it has the shape of one of our ids ("ann_" + base32).
inline std::string AnnotationIdOf(std::string_view rawInputJson) {
    StrictJson::Limits lim; lim.maxBytes = 64 * 1024; lim.maxDepth = 8;
    StrictJson::Value v; if (rawInputJson.empty() || !StrictJson::Parse(rawInputJson, v, nullptr, lim) || !v.IsObject()) return "";
    const auto* id = v.Get("id"); if (!id || !id->IsString() || id->string.size() < 5 || id->string.size() > 64 || id->string.rfind("ann_", 0) != 0) return "";
    for (char c : id->string) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return "";
    return id->string;
}
// One text argument of a call (« summary », « reason », « outcome »): empty when absent, not a string or too long.
inline std::string TextArg(std::string_view rawInputJson, const char* key, std::size_t max) {
    StrictJson::Limits lim; lim.maxBytes = 64 * 1024; lim.maxDepth = 8;
    StrictJson::Value v; if (rawInputJson.empty() || !StrictJson::Parse(rawInputJson, v, nullptr, lim) || !v.IsObject()) return "";
    const auto* t = v.Get(key); return t && t->IsString() && t->string.size() <= max ? t->string : std::string();
}
namespace detail {
// The JSON object inside a tool's result text: from its first « { » to its last « } » (an agent may wrap the text
// in a code fence or add a line around it).
inline bool ParseObjectIn(std::string_view text, StrictJson::Value& out) {
    const auto first = text.find('{'), last = text.rfind('}');
    if (first == std::string_view::npos || last == std::string_view::npos || last < first) return false;
    StrictJson::Limits lim; lim.maxBytes = 512 * 1024; lim.maxDepth = 16; lim.maxItems = 1u << 16;
    return StrictJson::Parse(text.substr(first, last - first + 1), out, nullptr, lim) && out.IsObject();
}
}
// (id, note) of every annotation named in a tool's result (the list's « annotations » array, or one annotation).
// The text is the agent's copy of our bridge's answer: read tolerantly, used for display only.
inline std::vector<std::pair<std::string, std::string>> AnnotationsIn(std::string_view resultText) {
    std::vector<std::pair<std::string, std::string>> out;
    StrictJson::Value v; if (!detail::ParseObjectIn(resultText, v)) return out;
    auto take = [&](const StrictJson::Value& a) {
        if (!a.IsObject() || out.size() >= 500) return;
        const auto* id = a.Get("id"); const auto* note = a.Get("note");
        if (id && id->IsString() && id->string.rfind("ann_", 0) == 0 && id->string.size() <= 64) out.emplace_back(id->string, note && note->IsString() ? note->string : std::string());
    };
    if (const auto* list = v.Get("annotations"); list && list->IsArray()) for (const auto& a : list->items) take(a);
    else take(v);
    return out;
}
// The list's own count: -1 when the result is not readable (then the step line carries no number).
inline int ListCount(std::string_view resultText) {
    StrictJson::Value v; if (!detail::ParseObjectIn(resultText, v)) return -1;
    const auto* list = v.Get("annotations"); return list && list->IsArray() ? int(list->items.size()) : -1;
}

} // namespace ChatTools
