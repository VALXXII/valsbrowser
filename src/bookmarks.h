#pragma once
#include "i18n.h"
// Bookmarks model (bookmarks bar and folders): an ordered tree of folders and
// links under two fixed roots, with stable ids, depth and cycle safety,
// subtree removal with undo snapshots, and a versioned text format. Pure C++17
// (no HWND, Direct2D or WebView2) so tests/bookmarks_test.cpp exercises it
// directly. See design/bookmarks-implementation-plan.md §2-§4 and §6.
//
// File `<dataDir>\bookmarks` (UTF-8, LF):
//   browser-bookmarks 1
//   <id>\t<parent>\t<L|F>\t<added>\t<title>\t<url>      one line per node, pre-order
//   end\t<node count>                                    trailer: proves the file is complete
// Roots are never written. Titles and URLs escape `\` `\t` `\n` `\r` as `\\` `\t`
// `\n` `\r` and lone UTF-16 surrogates as `\uXXXX`, so every title round-trips
// exactly. A parent always precedes its children, so no cycle can be loaded.
// #138 (a folder's icon and colour): once a folder carries one, the file is written as
// `browser-bookmarks 2`, each line with two more fields `\t<icon>\t<color>` (keys of
// src/bookmark-folder-style.h, empty = none; always empty for a link). A file where no
// folder has a look stays `browser-bookmarks 1`, byte for byte as before; both read.
// An older build never overwrites a version 2 file (ProfileStore: unknown format).
#include <algorithm>
#include <cstdint>
#include <istream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "address.h"

namespace Bookmarks {

using Id = std::uint32_t;                 // 0 = none
constexpr Id NoId = 0, BarRoot = 1, OtherRoot = 2; // fixed roots: never renamed, moved or removed
constexpr Id FirstUserId = 3;
constexpr std::size_t End = static_cast<std::size_t>(-1); // index meaning "append"
enum class Kind { Link, Folder };

struct Node {
    Id id = 0, parent = 0;
    Kind kind = Kind::Link;
    std::wstring title;                   // empty = show DisplayTitle() (MatchText(url))
    std::wstring url;                     // empty for a folder
    std::int64_t added = 0;               // Unix ms
    std::vector<Id> children;             // display order (folders only)
    std::string icon, color;              // folders only (#138): style keys, empty = the grey folder; unknown keys kept as read
    bool operator==(const Node& o) const { return id == o.id && parent == o.parent && kind == o.kind && title == o.title && url == o.url && added == o.added && children == o.children && icon == o.icon && color == o.color; }
    bool operator!=(const Node& o) const { return !(*this == o); }
};
// Undo snapshot of a removed subtree.
struct Removed {
    std::vector<Node> subtree;            // removed node first, then its descendants in pre-order
    Id parent = 0; std::size_t index = 0; // where it was
};

// Only http, https and file pages, under 4 096 characters (AddressBar::Recordable):
// javascript:, data:, blob:, about: and unknown schemes are refused everywhere.
inline bool Admissible(const std::wstring& url) { return AddressBar::Recordable(url); }
// Comparison key for "is this page bookmarked" (scheme/host case-insensitive, "/" root = none, fragment counts).
inline std::wstring UrlKey(const std::wstring& url) { return AddressBar::Key(url); }
// The header lines this build reads (ProfileStore's `known` list): version 2 adds a folder's look (#138).
inline const std::vector<std::string>& Headers() { static const std::vector<std::string> h{"browser-bookmarks 1", "browser-bookmarks 2"}; return h; }
// A style key as stored: empty (none) or 1-32 of a-z, 0-9, '-'. A key this build does not know (a later
// palette or icon set) is still a valid key: kept and written back, shown as the grey folder.
inline bool StyleKey(const std::string& key) {
    if (key.size() > 32) return false;
    for (char c : key) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}
// What the bar and menus show for a node: its title, or its readable address when untitled.
inline std::wstring DisplayTitle(const Node& n) {
    if (!n.title.empty() || n.kind == Kind::Folder) return n.title;
    return AddressBar::MatchText(n.url);
}

// ---- Text escaping (format detail, exposed for tests) ----------------------
inline std::string Escape(const std::wstring& s) {
    std::wstring e; e.reserve(s.size());
    static const wchar_t* digits = L"0123456789ABCDEF";
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c == L'\\') e += L"\\\\";
        else if (c == L'\t') e += L"\\t";
        else if (c == L'\n') e += L"\\n";
        else if (c == L'\r') e += L"\\r";
        else if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) { e += c; e += s[++i]; }
        else if (c >= 0xD800 && c <= 0xDFFF) { // lone surrogate: not encodable as UTF-8
            e += L"\\u";
            for (int shift = 12; shift >= 0; shift -= 4) e += digits[(static_cast<unsigned>(c) >> shift) & 15];
        } else e += c;
    }
    return NativeChrome::Downloads::ToUtf8(e);
}
inline bool Unescape(const std::string& utf8, std::wstring& out) {
    std::wstring raw;
    if (!NativeChrome::Downloads::FromUtf8(utf8, raw)) return false;
    out.clear(); out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        const wchar_t c = raw[i];
        if (c == L'\t' || c == L'\n' || c == L'\r') return false;
        if (c != L'\\') { out += c; continue; }
        if (++i >= raw.size()) return false;
        switch (raw[i]) {
        case L'\\': out += L'\\'; break;
        case L't': out += L'\t'; break;
        case L'n': out += L'\n'; break;
        case L'r': out += L'\r'; break;
        case L'u': {
            if (i + 4 >= raw.size()) return false;
            unsigned v = 0;
            for (int k = 1; k <= 4; ++k) {
                const wchar_t h = raw[i + k];
                const int d = h >= L'0' && h <= L'9' ? h - L'0' : h >= L'A' && h <= L'F' ? h - L'A' + 10 : h >= L'a' && h <= L'f' ? h - L'a' + 10 : -1;
                if (d < 0) return false;
                v = v * 16 + static_cast<unsigned>(d);
            }
            if (v < 0xD800 || v > 0xDFFF) return false; // only lone surrogates are escaped this way
            out += static_cast<wchar_t>(v); i += 4; break;
        }
        default: return false;
        }
    }
    return true;
}

class Store {
public:
    static constexpr std::size_t Limit = 10000;   // user nodes (roots excluded)
    static constexpr std::size_t MaxDepth = 8;    // folder levels, roots included (root = 1)
    static constexpr std::size_t TitleMax = 1024; // UTF-16 units

    Store() {
        nodes_[BarRoot] = Node{BarRoot, NoId, Kind::Folder, TR(bookmarks_bar_root), L"", 0, {}, {}, {}};   // roots are never written: named in the process language
        nodes_[OtherRoot] = Node{OtherRoot, NoId, Kind::Folder, TR(bookmarks_other_root), L"", 0, {}, {}, {}};
    }

    // ---- Queries -------------------------------------------------------------
    const Node* Find(Id id) const { auto it = nodes_.find(id); return it == nodes_.end() ? nullptr : &it->second; }
    // The fixed root `id` belongs to (a root returns itself; unknown ids return OtherRoot).
    const Node& Root(Id id) const {
        Id cur = id;
        for (std::size_t steps = 0; steps <= Limit + 2; ++steps) {
            if (cur == BarRoot || cur == OtherRoot) return nodes_.at(cur);
            const Node* n = Find(cur);
            if (!n) break;
            cur = n->parent;
        }
        return nodes_.at(OtherRoot);
    }
    static bool IsRoot(Id id) { return id == BarRoot || id == OtherRoot; }
    std::size_t Size() const { return nodes_.size() - 2; } // user nodes
    Id NextId() const { return next_; }
    // Whether `ancestor` is a strict ancestor of `node`. Bounded walk: never loops.
    bool IsAncestor(Id ancestor, Id node) const {
        const Node* n = Find(node);
        for (std::size_t steps = 0; n && steps <= MaxDepth + 1; ++steps) {
            if (n->parent == NoId) return false;
            if (n->parent == ancestor) return true;
            n = Find(n->parent);
        }
        return false;
    }
    // Roots are 1, their children 2, … ; 0 for an unknown id.
    int Depth(Id id) const {
        const Node* n = Find(id);
        int depth = 0;
        for (std::size_t steps = 0; n && steps <= MaxDepth + 2; ++steps) {
            ++depth;
            if (n->parent == NoId) return depth;
            n = Find(n->parent);
        }
        return 0;
    }
    // Links in a folder's whole subtree ("Ouvrir les 12 favoris"); 1 for a link.
    std::size_t CountLinks(Id id) const {
        std::size_t count = 0;
        for (Id x : Subtree(id)) if (nodes_.at(x).kind == Kind::Link) ++count;
        return count;
    }
    // Links whose URL matches `url` (UrlKey), most recently added first.
    std::vector<Id> Matching(const std::wstring& url) const {
        std::vector<Id> out;
        if (url.empty()) return out;
        const std::wstring key = UrlKey(url);
        for (auto& [id, n] : nodes_) if (n.kind == Kind::Link && UrlKey(n.url) == key) out.push_back(id);
        std::stable_sort(out.begin(), out.end(), [&](Id a, Id b) {
            const auto& na = nodes_.at(a); const auto& nb = nodes_.at(b);
            return na.added != nb.added ? na.added > nb.added : a > b;
        });
        return out;
    }
    // Index of `id` in its parent's children, End when unknown or a root.
    std::size_t IndexOf(Id id) const {
        const Node* n = Find(id);
        if (!n || IsRoot(id)) return End;
        const auto& siblings = nodes_.at(n->parent).children;
        const auto it = std::find(siblings.begin(), siblings.end(), id);
        return it == siblings.end() ? End : static_cast<std::size_t>(it - siblings.begin());
    }
    // `id` and every descendant, pre-order (explicit stack, visited ids skipped).
    std::vector<Id> Subtree(Id id) const {
        std::vector<Id> out;
        if (!Find(id)) return out;
        std::vector<Id> stack{id};
        std::map<Id, bool> seen;
        while (!stack.empty()) {
            const Id cur = stack.back(); stack.pop_back();
            const Node* n = Find(cur);
            if (!n || seen[cur]) continue;
            seen[cur] = true; out.push_back(cur);
            for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) stack.push_back(*it);
        }
        return out;
    }
    // Folder levels below `id`, `id` itself included: 1 for a lone folder, 0 for a link.
    int FolderHeight(Id id) const {
        const int base = Depth(id); int deepest = 0;
        for (Id x : Subtree(id)) if (nodes_.at(x).kind == Kind::Folder) deepest = std::max(deepest, Depth(x));
        return deepest == 0 ? 0 : deepest - base + 1;
    }

    // ---- Mutations -------------------------------------------------------------
    // Duplicates are allowed (same page in two folders): the caller decides, via
    // Matching(), whether Ctrl+D edits an existing bookmark instead of adding one.
    Id Add(Id parent, std::size_t index, std::wstring title, std::wstring url, std::int64_t now) {
        if (!Admissible(url) || !CanHold(parent) || !HasRoom()) return NoId;
        return Insert(parent, index, Kind::Link, Clip(std::move(title)), std::move(url), now);
    }
    Id AddFolder(Id parent, std::size_t index, std::wstring title, std::int64_t now) {
        if (!CanHold(parent) || !HasRoom() || static_cast<std::size_t>(Depth(parent)) + 1 > MaxDepth) return NoId;
        return Insert(parent, index, Kind::Folder, Clip(std::move(title)), L"", now);
    }
    bool Rename(Id id, std::wstring title) {
        auto* n = Mutable(id);
        if (!n || IsRoot(id)) return false;
        n->title = Clip(std::move(title));
        return true;
    }
    bool SetUrl(Id id, const std::wstring& url) {
        auto* n = Mutable(id);
        if (!n || n->kind != Kind::Link || !Admissible(url)) return false;
        n->url = url;
        return true;
    }
    // A folder's look (#138): icon and colour keys, empty = none. Roots and links have none.
    bool SetStyle(Id id, const std::string& icon, const std::string& color) {
        auto* n = Mutable(id);
        if (!n || IsRoot(id) || n->kind != Kind::Folder || !StyleKey(icon) || !StyleKey(color)) return false;
        n->icon = icon; n->color = color;
        return true;
    }
    // Whether a folder carries a look (the file is then written as version 2).
    bool Styled() const { for (const auto& [id, n] : nodes_) if (!n.icon.empty() || !n.color.empty()) return true; return false; }
    // `index` is the final position among newParent's children (clamped; End appends).
    bool Move(Id id, Id newParent, std::size_t index) {
        Node* n = Mutable(id);
        if (!n || IsRoot(id) || id == newParent || !CanHold(newParent) || IsAncestor(id, newParent)) return false;
        if (n->kind == Kind::Folder && static_cast<std::size_t>(Depth(newParent) + FolderHeight(id)) > MaxDepth) return false;
        Detach(id);
        n->parent = newParent;
        Attach(newParent, index, id);
        return true;
    }
    std::optional<Removed> Remove(Id id) {
        const Node* n = Find(id);
        if (!n || IsRoot(id)) return std::nullopt;
        Removed r; r.parent = n->parent; r.index = IndexOf(id);
        for (Id x : Subtree(id)) r.subtree.push_back(nodes_.at(x));
        Detach(id);
        for (const auto& x : r.subtree) nodes_.erase(x.id);
        return r;
    }
    // Puts a removed subtree back with the same ids, at the same parent and index
    // (clamped). When that parent is gone (or can no longer take the subtree's
    // depth) it goes to the end of OtherRoot. Refuses snapshots whose ids are in
    // use, that are inconsistent, or that would pass Limit.
    bool Restore(const Removed& r) {
        if (r.subtree.empty() || IsRoot(r.subtree[0].id) || Size() + r.subtree.size() > Limit) return false;
        std::map<Id, std::size_t> at;
        for (std::size_t i = 0; i < r.subtree.size(); ++i) {
            const Node& x = r.subtree[i];
            if (x.id < FirstUserId || Find(x.id) || at.count(x.id)) return false;
            if (x.kind == Kind::Link ? (!Admissible(x.url) || !x.children.empty()) : !x.url.empty()) return false;
            if (i > 0 && (!at.count(x.parent) || r.subtree[at[x.parent]].kind != Kind::Folder)) return false;
            at[x.id] = i;
        }
        // Every child listed must be in the snapshot under that parent, each listed once.
        std::size_t listed = 0; std::map<Id, bool> once;
        for (const auto& x : r.subtree) for (Id c : x.children) {
            if (!at.count(c) || r.subtree[at[c]].parent != x.id || at[c] == 0 || once[c]) return false;
            once[c] = true; ++listed;
        }
        if (listed != r.subtree.size() - 1) return false;
        int height = 0; // folder levels of the snapshot
        if (r.subtree[0].kind == Kind::Folder) {
            std::map<Id, int> level; level[r.subtree[0].id] = 1; height = 1;
            for (std::size_t i = 1; i < r.subtree.size(); ++i) {
                const Node& x = r.subtree[i];
                level[x.id] = level[x.parent] + 1;
                if (x.kind == Kind::Folder) height = std::max(height, level[x.id]);
            }
        }
        Id parent = r.parent; std::size_t index = r.index;
        if (!CanHold(parent) || static_cast<std::size_t>(Depth(parent) + height) > MaxDepth) { parent = OtherRoot; index = End; }
        if (static_cast<std::size_t>(Depth(parent) + height) > MaxDepth) return false;
        for (const auto& x : r.subtree) {
            nodes_[x.id] = x;
            if (x.id >= next_ && x.id != std::numeric_limits<Id>::max()) next_ = x.id + 1;
        }
        nodes_[r.subtree[0].id].parent = parent;
        Attach(parent, index, r.subtree[0].id);
        return true;
    }

    // ---- Persistence -----------------------------------------------------------
    void Write(std::ostream& out) const {
        const bool v2 = Styled();   // no look anywhere: the version 1 file, unchanged
        out << (v2 ? "browser-bookmarks 2\n" : "browser-bookmarks 1\n");
        std::size_t count = 0;
        for (Id root : {BarRoot, OtherRoot}) for (Id top : nodes_.at(root).children) for (Id id : Subtree(top)) {
            const Node& n = nodes_.at(id);
            out << n.id << '\t' << n.parent << '\t' << (n.kind == Kind::Folder ? 'F' : 'L') << '\t' << n.added << '\t' << Escape(n.title) << '\t' << Escape(n.url);
            if (v2) out << '\t' << n.icon << '\t' << n.color;
            out << '\n';
            ++count;
        }
        out << "end\t" << count << '\n';
    }
    // Tolerant, line by line. `damaged` is set whenever anything was dropped or
    // repaired: unknown header, malformed or truncated line, missing/incorrect
    // trailer, duplicate or reserved id, refused URL, unknown or too-deep parent
    // (the node goes to OtherRoot), excess past Limit. An empty stream is a
    // missing file: an empty store, not damaged.
    static Store Read(std::istream& in, bool* damaged = nullptr) {
        Store s; bool bad = false;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (damaged) *damaged = false;
        if (text.empty()) return s;
        std::vector<std::string> lines;
        std::size_t start = 0;
        for (std::size_t nl; (nl = text.find('\n', start)) != std::string::npos; start = nl + 1) lines.push_back(text.substr(start, nl - start));
        if (start < text.size()) bad = true; // last line cut before its newline: dropped
        for (auto& l : lines) if (!l.empty() && l.back() == '\r') l.pop_back();
        const int version = lines.empty() ? 0 : lines[0] == "browser-bookmarks 2" ? 2 : lines[0] == "browser-bookmarks 1" ? 1 : 0;
        if (!version) { if (damaged) *damaged = true; return s; }
        bool ended = false; std::size_t nodeLines = 0;
        for (std::size_t i = 1; i < lines.size(); ++i) {
            const std::string& line = lines[i];
            if (ended) { bad = true; break; }
            if (line.rfind("end\t", 0) == 0) {
                std::uint64_t n = 0;
                if (!ParseUnsigned(line.substr(4), n) || n != nodeLines) bad = true;
                ended = true; continue;
            }
            ++nodeLines;
            if (s.Size() >= Limit) { bad = true; continue; }
            if (!s.ReadLine(line, version)) bad = true;
        }
        if (!ended) bad = true;
        if (damaged) *damaged = bad;
        return s;
    }

    // ---- Interprocess merge (one process per window, decision 2) ----------------------
    // Three-way merge run under the profile lock: `theirs` is the file as it is now, `base` what this
    // process last read or wrote, `mine` its current tree. The changes base→mine are replayed onto
    // theirs, so another window's additions, edits and deletions are kept and a deletion is never
    // resurrected. A node is the same node only with the same id, kind and `added` time: an edit or a
    // removal never lands on a different node that reused an id. New ids taken elsewhere are remapped
    // (`remap`: old → new). Moves keep the position after the previous sibling.
    static Store Merge(const Store& base, const Store& mine, const Store& theirs, std::map<Id, Id>* remap = nullptr) {
        Store r = theirs;
        auto same = [](const Node* a, const Node* b) { return a && b && a->kind == b->kind && a->added == b->added; };
        for (const auto& [id, bn] : base.nodes_) {   // 1. removals
            if (IsRoot(id) || mine.Find(id)) continue;
            if (same(r.Find(id), &bn)) r.Remove(id);
        }
        std::map<Id, Id> map; Id fresh = std::max(r.next_, mine.next_);
        for (const auto& [id, mn] : mine.nodes_) {   // 2. ids of the nodes new in mine
            if (IsRoot(id) || same(base.Find(id), &mn)) continue;
            if (r.Find(id)) { while (r.Find(fresh) || mine.Find(fresh)) ++fresh; map[id] = fresh++; }
        }
        auto mapped = [&](Id id) { const auto it = map.find(id); return it == map.end() ? id : it->second; };
        auto previous = [](const Store& s, Id id) -> Id {
            const Node* n = s.Find(id); if (!n) return NoId;
            const auto& sib = s.nodes_.at(n->parent).children; const auto it = std::find(sib.begin(), sib.end(), id);
            return it == sib.begin() || it == sib.end() ? NoId : *(it - 1);
        };
        for (Id root : {BarRoot, OtherRoot}) for (Id top : mine.nodes_.at(root).children) for (Id id : mine.Subtree(top)) {   // 3. pre-order
            const Node& mn = mine.nodes_.at(id); const Node* bn = base.Find(id);
            const bool existing = same(bn, &mn);
            Id parent = mapped(mn.parent);
            if (!r.CanHold(parent)) parent = OtherRoot;   // its folder was deleted in another window
            auto position = [&]() -> std::size_t {   // just after the nearest previous sibling present there
                const auto& sib = mine.nodes_.at(mn.parent).children; auto it = std::find(sib.begin(), sib.end(), id);
                while (it != sib.begin()) { --it; const Node* q = r.Find(mapped(*it)); if (q && q->parent == parent) return r.IndexOf(q->id) + 1; }
                return 0;
            };
            if (existing) {
                Node* rn = r.Mutable(id);
                if (!same(rn, bn)) continue;   // deleted or replaced elsewhere: this edit has nothing to apply to
                if (mn.title != bn->title) rn->title = mn.title;
                if (mn.url != bn->url) rn->url = mn.url;
                if (mn.icon != bn->icon) rn->icon = mn.icon;      // #138: a look changed here wins; one changed elsewhere stays
                if (mn.color != bn->color) rn->color = mn.color;
                if (mn.parent != bn->parent || previous(mine, id) != previous(base, id)) {
                    std::size_t at = position();
                    if (r.Find(id)->parent == parent && r.IndexOf(id) < at) --at;
                    r.Move(id, parent, at);
                }
            } else {
                if (r.Size() >= Limit) continue;
                if (mn.kind == Kind::Folder && static_cast<std::size_t>(r.Depth(parent)) + 1 > MaxDepth) parent = OtherRoot;
                const Id nid = mapped(id); const std::size_t at = position();
                r.nodes_[nid] = Node{nid, parent, mn.kind, mn.title, mn.url, mn.added, {}, mn.icon, mn.color};
                r.Attach(parent, at, nid);
                if (nid >= r.next_) r.next_ = nid + 1;
            }
        }
        r.next_ = std::max({r.next_, mine.next_, fresh});
        if (remap) *remap = map;
        return r;
    }

    bool operator==(const Store& o) const { return nodes_ == o.nodes_; }
    bool operator!=(const Store& o) const { return !(*this == o); }

private:
    std::map<Id, Node> nodes_;
    Id next_ = FirstUserId; // never reused during a session: max id seen + 1

    Node* Mutable(Id id) { auto it = nodes_.find(id); return it == nodes_.end() ? nullptr : &it->second; }
    bool CanHold(Id parent) const { const Node* p = Find(parent); return p && p->kind == Kind::Folder; }
    bool HasRoom() const { return Size() < Limit && next_ != std::numeric_limits<Id>::max(); }
    static std::wstring Clip(std::wstring title) {
        if (title.size() > TitleMax) {
            std::size_t cut = TitleMax;
            if (title[cut - 1] >= 0xD800 && title[cut - 1] <= 0xDBFF) --cut; // keep surrogate pairs whole
            title.resize(cut);
        }
        return title;
    }
    Id Insert(Id parent, std::size_t index, Kind kind, std::wstring title, std::wstring url, std::int64_t now) {
        const Id id = next_++;
        nodes_[id] = Node{id, parent, kind, std::move(title), std::move(url), now, {}, {}, {}};
        Attach(parent, index, id);
        return id;
    }
    void Attach(Id parent, std::size_t index, Id id) {
        auto& c = nodes_.at(parent).children;
        c.insert(c.begin() + static_cast<std::ptrdiff_t>(std::min(index, c.size())), id);
    }
    void Detach(Id id) {
        auto& c = nodes_.at(nodes_.at(id).parent).children;
        c.erase(std::remove(c.begin(), c.end(), id), c.end());
    }
    static bool ParseUnsigned(const std::string& s, std::uint64_t& out) {
        if (s.empty() || s.size() > 19) return false;
        out = 0;
        for (char c : s) { if (c < '0' || c > '9') return false; out = out * 10 + static_cast<unsigned>(c - '0'); }
        return true;
    }
    static bool ParseSigned(const std::string& s, std::int64_t& out) {
        const bool negative = !s.empty() && s[0] == '-';
        std::uint64_t v = 0;
        if (!ParseUnsigned(negative ? s.substr(1) : s, v) || v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
        out = negative ? -static_cast<std::int64_t>(v) : static_cast<std::int64_t>(v);
        return true;
    }
    // One node line (version 1: 6 fields; version 2: 8, the folder's look last). False when the line was dropped or repaired.
    bool ReadLine(const std::string& line, int version = 1) {
        std::vector<std::string> f; std::size_t start = 0;
        for (std::size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1) f.push_back(line.substr(start, tab - start));
        f.push_back(line.substr(start));
        if (f.size() != (version == 2 ? 8u : 6u) || f[2].size() != 1 || (f[2][0] != 'L' && f[2][0] != 'F')) return false;
        std::uint64_t id = 0, parent = 0; std::int64_t added = 0;
        if (!ParseUnsigned(f[0], id) || !ParseUnsigned(f[1], parent) || !ParseSigned(f[3], added)) return false;
        if (id < FirstUserId || id >= std::numeric_limits<Id>::max() || parent > std::numeric_limits<Id>::max() || Find(static_cast<Id>(id))) return false;
        Node n; n.id = static_cast<Id>(id); n.kind = f[2][0] == 'F' ? Kind::Folder : Kind::Link; n.added = added;
        if (!Unescape(f[4], n.title) || !Unescape(f[5], n.url)) return false;
        if (n.kind == Kind::Link ? !Admissible(n.url) : !n.url.empty()) return false;
        bool clean = true;
        if (n.title.size() > TitleMax) { n.title = Clip(std::move(n.title)); clean = false; }
        if (version == 2 && (!f[6].empty() || !f[7].empty())) {   // a look: folders only, well-formed keys; else the line loads without it
            if (n.kind == Kind::Folder && StyleKey(f[6]) && StyleKey(f[7])) { n.icon = f[6]; n.color = f[7]; } else clean = false;
        }
        n.parent = static_cast<Id>(parent);
        // A parent must already be loaded (pre-order): cycles cannot load.
        if (!CanHold(n.parent) || (n.kind == Kind::Folder && static_cast<std::size_t>(Depth(n.parent)) + 1 > MaxDepth)) { n.parent = OtherRoot; clean = false; }
        const Id nid = n.id, p = n.parent;
        nodes_[nid] = std::move(n);
        Attach(p, End, nid);
        if (nid >= next_) next_ = nid + 1;
        return clean;
    }
};

} // namespace Bookmarks
