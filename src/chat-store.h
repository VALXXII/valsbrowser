#pragma once
// Chat intégré — what the browser remembers (design/chat-integration.md, « Stockage »). Local files only, in the
// application's data root, never in the project and never in an agent's configuration:
//   <data root>\annotations-chat\projects.json                 origin -> { folder, agentId, mode, model, effort, usedAt }
//   <data root>\annotations-chat\conversations\index.json      the history list (one row per conversation)
//   <data root>\annotations-chat\conversations\<id>.json       one conversation: header + the panel's items (contract B §4.2)
// Written atomically (temp file + rename), read with the strict JSON parser and bounds; a damaged file reads as empty
// (the conversation is lost, nothing else). No file is touched before the chat panel is opened for the first time.
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "strict-json.h"

namespace ChatStore {
namespace SJ = StrictJson;

inline constexpr std::size_t MaxConversations = 200, MaxItems = 4000, MaxFileBytes = 24u << 20;

inline std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {}; const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr); return s;
}
inline std::wstring Wide(const std::string& s) {
    if (s.empty()) return {}; const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n); return w;
}
inline bool ReadFile(const std::wstring& path, std::string& out, std::size_t max = MaxFileBytes) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && std::uint64_t(size.QuadPart) <= max;
    if (ok) { out.resize(size_t(size.QuadPart)); DWORD got = 0, done = 0; while (ok && done < out.size()) { ok = ::ReadFile(f, out.data() + done, DWORD(std::min<std::size_t>(out.size() - done, 1u << 20)), &got, nullptr) && got > 0; done += got; } }
    CloseHandle(f); return ok;
}
inline bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = true; std::size_t done = 0;
    while (ok && done < data.size()) { DWORD put = 0; ok = ::WriteFile(f, data.data() + done, DWORD(std::min<std::size_t>(data.size() - done, 1u << 20)), &put, nullptr) && put > 0; done += put; }
    ok = ok && FlushFileBuffers(f); CloseHandle(f);
    if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp.c_str());
    return ok;
}
inline void EnsureDir(const std::wstring& dir) { if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() == ERROR_PATH_NOT_FOUND) { const auto s = dir.find_last_of(L'\\'); if (s != std::wstring::npos) { EnsureDir(dir.substr(0, s)); CreateDirectoryW(dir.c_str(), nullptr); } } }
inline std::string Str(const SJ::Value& v, const char* key, std::size_t max = 4096) { const auto* p = v.Get(key); return p && p->IsString() && p->string.size() <= max ? p->string : std::string(); }
inline std::int64_t Int(const SJ::Value& v, const char* key) { const auto* p = v.Get(key); return p && p->IsNumber() && p->integral ? p->integer : 0; }
// Identifiers of the panel protocol: [A-Za-z0-9._:-]{1,128}.
inline bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-')) return false;
    return true;
}

// ---------------------------------------------------------------- site -> project folder
struct Project { std::wstring folder; std::string agentId, mode, model, effort; std::int64_t usedAt = 0; };
struct Projects {
    std::map<std::string, Project> sites;   // key: the origin ("http://localhost:5173")
    static Projects Load(const std::wstring& file) {
        Projects p; std::string text; SJ::Value v;
        if (!ReadFile(file, text, 1u << 20) || !SJ::Parse(text, v) || !v.IsObject()) return p;
        const auto* sites = v.Get("sites"); if (!sites || !sites->IsObject()) return p;
        for (const auto& m : sites->members) {
            if (m.first.empty() || m.first.size() > 300 || !m.second.IsObject() || p.sites.size() >= 500) continue;
            Project pr; pr.folder = Wide(Str(m.second, "folder", 2048)); pr.agentId = Str(m.second, "agentId", 128); pr.mode = Str(m.second, "mode", 128);
            pr.model = Str(m.second, "model", 128); pr.effort = Str(m.second, "effort", 128); pr.usedAt = Int(m.second, "usedAt");
            p.sites[m.first] = std::move(pr);
        }
        return p;
    }
    bool Save(const std::wstring& file) const {
        SJ::Value v = SJ::Value::Obj(); v.Set("v", SJ::Value::Int(1)); SJ::Value sites = SJ::Value::Obj();
        for (const auto& s : this->sites) {
            SJ::Value o = SJ::Value::Obj(); o.Set("folder", SJ::Value::Str(Utf8(s.second.folder)));
            if (!s.second.agentId.empty()) o.Set("agentId", SJ::Value::Str(s.second.agentId));
            if (!s.second.mode.empty()) o.Set("mode", SJ::Value::Str(s.second.mode));
            if (!s.second.model.empty()) o.Set("model", SJ::Value::Str(s.second.model));
            if (!s.second.effort.empty()) o.Set("effort", SJ::Value::Str(s.second.effort));
            o.Set("usedAt", SJ::Value::Int(s.second.usedAt)); sites.Set(s.first, std::move(o));
        }
        v.Set("sites", std::move(sites));
        return WriteFileAtomic(file, SJ::Serialize(v) + "\n");
    }
    // The folder used most recently for any site (the Windows picker opens there).
    std::wstring Recent() const { const Project* best = nullptr; for (const auto& s : sites) if (!s.second.folder.empty() && (!best || s.second.usedAt > best->usedAt)) best = &s.second; return best ? best->folder : std::wstring(); }
};

// ---------------------------------------------------------------- conversations (history per site)
struct Header {
    std::string id, origin, siteLabel, title, agentId, sessionId, source;   // source: "Checkout.astro:42" of the first aimed element
    std::wstring folder; std::int64_t createdAt = 0, updatedAt = 0; int promptVersion = 0;
    std::int64_t usedTokens = 0, windowTokens = 0;   // last context usage the agent reported (the panel's gauge)
    SJ::Value ToJson() const {
        SJ::Value o = SJ::Value::Obj(); o.Set("id", SJ::Value::Str(id)); o.Set("origin", SJ::Value::Str(origin)); o.Set("siteLabel", SJ::Value::Str(siteLabel));
        o.Set("title", SJ::Value::Str(title)); o.Set("agentId", SJ::Value::Str(agentId)); o.Set("sessionId", SJ::Value::Str(sessionId)); o.Set("source", SJ::Value::Str(source));
        o.Set("folder", SJ::Value::Str(Utf8(folder))); o.Set("createdAt", SJ::Value::Int(createdAt)); o.Set("updatedAt", SJ::Value::Int(updatedAt)); o.Set("promptVersion", SJ::Value::Int(promptVersion));
        if (windowTokens > 0) { o.Set("usedTokens", SJ::Value::Int(usedTokens)); o.Set("windowTokens", SJ::Value::Int(windowTokens)); }
        return o;
    }
    static bool FromJson(const SJ::Value& v, Header& h) {
        if (!v.IsObject()) return false;
        h.id = Str(v, "id", 128); if (!ValidId(h.id)) return false;
        h.origin = Str(v, "origin", 300); h.siteLabel = Str(v, "siteLabel", 300); h.title = Str(v, "title", 400); h.agentId = Str(v, "agentId", 128);
        h.sessionId = Str(v, "sessionId", 512); h.source = Str(v, "source", 300); h.folder = Wide(Str(v, "folder", 2048));
        h.createdAt = Int(v, "createdAt"); h.updatedAt = Int(v, "updatedAt"); h.promptVersion = int(Int(v, "promptVersion"));
        h.usedTokens = std::max<std::int64_t>(0, Int(v, "usedTokens")); h.windowTokens = std::max<std::int64_t>(0, Int(v, "windowTokens"));
        return true;
    }
};
struct Conversation { Header head; std::vector<SJ::Value> items; };   // items: host -> panel messages, finished (contract B §4.2)

class History {
    std::wstring dir_;
    std::wstring IndexFile() const { return dir_ + L"\\index.json"; }
    std::wstring FileOf(const std::string& id) const { return dir_ + L"\\" + Wide(id) + L".json"; }
public:
    explicit History(std::wstring dataRoot) : dir_(std::move(dataRoot) + L"\\annotations-chat\\conversations") {}
    const std::wstring& Dir() const { return dir_; }
    std::vector<Header> List() const {   // most recent first
        std::vector<Header> out; std::string text; SJ::Value v;
        if (!ReadFile(IndexFile(), text, 4u << 20) || !SJ::Parse(text, v) || !v.IsObject()) return out;
        if (const auto* rows = v.Get("conversations"); rows && rows->IsArray())
            for (const auto& r : rows->items) { Header h; if (out.size() < MaxConversations && Header::FromJson(r, h)) out.push_back(std::move(h)); }
        std::stable_sort(out.begin(), out.end(), [](const Header& a, const Header& b) { return a.updatedAt > b.updatedAt; });
        return out;
    }
    bool Load(const std::string& id, Conversation& out) const {
        if (!ValidId(id)) return false;
        std::string text; SJ::Value v; SJ::Limits lim; lim.maxBytes = MaxFileBytes; lim.maxStringBytes = 4u << 20; lim.maxItems = 1u << 20; lim.maxDepth = 24;
        if (!ReadFile(FileOf(id), text) || !SJ::Parse(text, v, nullptr, lim) || !v.IsObject()) return false;
        const auto* head = v.Get("head"); if (!head || !Header::FromJson(*head, out.head) || out.head.id != id) return false;
        out.items.clear();
        if (const auto* items = v.Get("items"); items && items->IsArray())
            for (const auto& i : items->items) if (i.IsObject() && out.items.size() < MaxItems) out.items.push_back(i);
        return true;
    }
    // Writes the conversation and its row of the index; the oldest conversations beyond MaxConversations are removed.
    bool Save(const Conversation& c) const {
        if (!ValidId(c.head.id)) return false;
        EnsureDir(dir_);
        SJ::Value v = SJ::Value::Obj(); v.Set("v", SJ::Value::Int(1)); v.Set("head", c.head.ToJson());
        SJ::Value items = SJ::Value::Arr(); const std::size_t from = c.items.size() > MaxItems ? c.items.size() - MaxItems : 0;
        for (std::size_t i = from; i < c.items.size(); ++i) items.Push(c.items[i]);
        v.Set("items", std::move(items));
        const std::string text = SJ::Serialize(v) + "\n";
        if (text.size() > MaxFileBytes || !WriteFileAtomic(FileOf(c.head.id), text)) return false;
        auto rows = List(); rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Header& h) { return h.id == c.head.id; }), rows.end());
        rows.insert(rows.begin(), c.head);
        std::stable_sort(rows.begin(), rows.end(), [](const Header& a, const Header& b) { return a.updatedAt > b.updatedAt; });
        while (rows.size() > MaxConversations) { DeleteFileW(FileOf(rows.back().id).c_str()); rows.pop_back(); }
        SJ::Value idx = SJ::Value::Obj(); idx.Set("v", SJ::Value::Int(1)); SJ::Value arr = SJ::Value::Arr(); for (const auto& h : rows) arr.Push(h.ToJson()); idx.Set("conversations", std::move(arr));
        return WriteFileAtomic(IndexFile(), SJ::Serialize(idx) + "\n");
    }
    bool Remove(const std::string& id) const {
        if (!ValidId(id)) return false;
        auto rows = List(); rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Header& h) { return h.id == id; }), rows.end());
        SJ::Value idx = SJ::Value::Obj(); idx.Set("v", SJ::Value::Int(1)); SJ::Value arr = SJ::Value::Arr(); for (const auto& h : rows) arr.Push(h.ToJson()); idx.Set("conversations", std::move(arr));
        DeleteFileW(FileOf(id).c_str());
        return WriteFileAtomic(IndexFile(), SJ::Serialize(idx) + "\n");
    }
};

} // namespace ChatStore
