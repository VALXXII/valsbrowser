// PERF-01: the history store indexes its pages by key. This test drives the indexed History
// (src/address.h) and a verbatim copy of the previous linear-scan implementation with the same
// 5 000+ operations (records with eviction, imports, titles, removals, file read, three-way merges)
// and checks that both give exactly the same pages in the same order, then prints both timings.
#include "../src/address.h"
#include "../src/profile-store.h"
#include <chrono>
#include <cstdio>
#include <sstream>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
using namespace AddressBar;

// The previous implementation (before PERF-01), kept here as the reference.
struct OldHistory {
    std::vector<Visit> items_;
    static constexpr size_t Limit = History::Limit;
    static std::wstring Clean(std::wstring s) { for (auto& c : s) if (c == L'\t' || c == L'\n' || c == L'\r') c = L' '; return s; }
    Visit* Find(const std::wstring& url) { const auto key = Key(url); for (auto& v : items_) if (Key(v.url) == key) return &v; return nullptr; }
    void Evict() {
        auto worst = std::min_element(items_.begin(), items_.end(), [](const Visit& a, const Visit& b) { return a.visits != b.visits ? a.visits < b.visits : a.last < b.last; });
        if (worst != items_.end()) items_.erase(worst);
    }
    bool Record(const std::wstring& url, const std::wstring& title, std::int64_t now) {
        if (!Recordable(url)) return false;
        auto* v = Find(url);
        if (!v) { if (items_.size() >= Limit) Evict(); items_.push_back({url, L"", 0, 0}); v = &items_.back(); }
        ++v->visits; v->last = now; if (!title.empty()) v->title = title; return true;
    }
    void Import(const std::wstring& url, const std::wstring& title, int visits, std::int64_t last) {
        if (!Recordable(url) || last <= 0) return;
        if (auto* v = Find(url)) { v->visits = std::max(v->visits, std::max(1, visits)); v->last = std::max(v->last, last); if (v->title.empty() && !title.empty()) v->title = Clean(title); return; }
        if (items_.size() >= Limit) return;
        items_.push_back({url, Clean(title), std::max(1, visits), last});
    }
    bool SetTitle(const std::wstring& url, const std::wstring& title) { auto* v = Find(url); if (!v || title.empty() || v->title == title) return false; v->title = title; return true; }
    bool Remove(const std::wstring& url) {
        const auto key = Key(url); const auto before = items_.size();
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Visit& v) { return Key(v.url) == key; }), items_.end());
        return items_.size() != before;
    }
    bool RemoveHost(const std::wstring& host) {
        const auto before = items_.size();
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Visit& v) { Parts p; return Split(v.url, p) && ReadableHost(p.host) == host; }), items_.end());
        return items_.size() != before;
    }
    static OldHistory Merge(const OldHistory& base, const OldHistory& mine, const OldHistory& theirs) {
        OldHistory r = theirs;
        for (const auto& b : base.items_) if (!const_cast<OldHistory&>(mine).Find(b.url)) r.Remove(b.url);
        for (const auto& m : mine.items_) {
            const Visit* b = const_cast<OldHistory&>(base).Find(m.url);
            const int fresh = b ? std::max(0, m.visits - b->visits) : m.visits;
            Visit* v = r.Find(m.url);
            if (v) { v->visits += fresh; if (m.last > v->last) { v->last = m.last; if (!m.title.empty()) v->title = m.title; } else if (b && m.title != b->title && !m.title.empty()) v->title = m.title; }
            else if (fresh > 0) { if (r.items_.size() >= Limit) r.Evict(); r.items_.push_back({m.url, m.title, fresh, m.last}); }
        }
        return r;
    }
    static OldHistory Read(std::istream& in) {
        OldHistory h; std::string line;
        if (!std::getline(in, line)) return h;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != "browser-history 1") return h;
        while (std::getline(in, line) && h.items_.size() < Limit) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> f; size_t start = 0;
            for (size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1) f.push_back(line.substr(start, tab - start));
            f.push_back(line.substr(start));
            if (f.size() != 4) continue;
            Visit v;
            try { v.visits = std::max(1, std::stoi(f[0])); v.last = std::stoll(f[1]); } catch (...) { continue; }
            if (!NativeChrome::Downloads::FromUtf8(f[2], v.url) || !NativeChrome::Downloads::FromUtf8(f[3], v.title) || !Recordable(v.url) || h.Find(v.url)) continue;
            h.items_.push_back(std::move(v));
        }
        return h;
    }
};
static bool Same(const History& a, const OldHistory& b) {
    if (a.Items().size() != b.items_.size()) return false;
    for (size_t i = 0; i < b.items_.size(); ++i) { const auto& x = a.Items()[i]; const auto& y = b.items_[i]; if (x.url != y.url || x.title != y.title || x.visits != y.visits || x.last != y.last) return false; }
    return true;
}
// Varied spellings of the same pages: case of scheme/host, "/" root, www., several hosts.
static std::wstring Url(unsigned n) {
    const unsigned page = n % 1700, host = page % 37;
    std::wstring h = L"site" + std::to_wstring(host) + L".example";
    std::wstring scheme = (n / 1700) % 2 ? L"HTTPS://" : L"https://";
    if ((n / 3400) % 2) for (auto& c : h) c = static_cast<wchar_t>(towupper(c));
    return scheme + h + (page < 37 ? std::wstring((n % 2) ? L"/" : L"") : L"/p/" + std::to_wstring(page));
}
template <class F> static double Ms(F f) { const auto t0 = std::chrono::steady_clock::now(); f(); return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }

int main() {
    constexpr unsigned Ops = 6000;   // >= 5 000 entries pass through both stores
    History now; OldHistory old;
    std::uint32_t seed = 12345; auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    double tNew = 0, tOld = 0;
    // Records (with eviction once 1 000 pages are kept), titles, imports, removals.
    std::vector<unsigned> ops; for (unsigned i = 0; i < Ops; ++i) ops.push_back(rnd());
    tNew += Ms([&] { for (unsigned i = 0; i < Ops; ++i) { const unsigned r = ops[i]; const auto u = Url(r % 5100);
        switch (r % 23) { case 0: now.SetTitle(u, L"T" + std::to_wstring(i)); break; case 1: now.Remove(u); break; case 2: now.Import(u, L"Imp\t" + std::to_wstring(i), int(r % 9), i + 1); break; case 3: if (i % 400 == 0) now.RemoveHost(L"site3.example"); break; default: now.Record(u, (r % 3) ? L"" : L"Title " + std::to_wstring(i), i + 1); } } });
    tOld += Ms([&] { for (unsigned i = 0; i < Ops; ++i) { const unsigned r = ops[i]; const auto u = Url(r % 5100);
        switch (r % 23) { case 0: old.SetTitle(u, L"T" + std::to_wstring(i)); break; case 1: old.Remove(u); break; case 2: old.Import(u, L"Imp\t" + std::to_wstring(i), int(r % 9), i + 1); break; case 3: if (i % 400 == 0) old.RemoveHost(L"site3.example"); break; default: old.Record(u, (r % 3) ? L"" : L"Title " + std::to_wstring(i), i + 1); } } });
    CHECK(Same(now, old)); CHECK(now.Items().size() == History::Limit);

    // A 5 000-line file (with duplicate spellings), read by both.
    std::ostringstream file; file << "browser-history 1\n";
    for (unsigned i = 0; i < 5000; ++i) file << (1 + i % 7) << '\t' << (1000 + i) << '\t' << NativeChrome::Downloads::ToUtf8(Url(i * 7919u % 5100)) << '\t' << "Page " << i << '\n';
    History readNew; OldHistory readOld;
    tNew += Ms([&] { std::istringstream in(file.str()); readNew = History::Read(in); });
    tOld += Ms([&] { std::istringstream in(file.str()); readOld = OldHistory::Read(in); });
    CHECK(Same(readNew, readOld)); CHECK(!readNew.Items().empty());

    // Three-way merges at the cap: this window's records and removals onto another window's file.
    History base = readNew, mine = readNew; OldHistory baseOld = readOld, mineOld = readOld;
    for (unsigned i = 0; i < 300; ++i) { const auto u = Url(rnd() % 5100); if (i % 5 == 0) { mine.Remove(u); mineOld.Remove(u); } else { mine.Record(u, L"M" + std::to_wstring(i), 9000 + i); mineOld.Record(u, L"M" + std::to_wstring(i), 9000 + i); } }
    History merged; OldHistory mergedOld;
    for (int round = 0; round < 5; ++round) {
        tNew += Ms([&] { merged = History::Merge(base, mine, now); });
        tOld += Ms([&] { mergedOld = OldHistory::Merge(baseOld, mineOld, old); });
        CHECK(Same(merged, mergedOld));
    }
    // Writing is unchanged: same bytes from both.
    std::ostringstream a; merged.Write(a); std::string b = "browser-history 1\n";
    for (auto& v : mergedOld.items_) b += std::to_string(v.visits) + '\t' + std::to_string(v.last) + '\t' + NativeChrome::Downloads::ToUtf8(v.url) + '\t' + NativeChrome::Downloads::ToUtf8(OldHistory::Clean(v.title)) + '\n';
    CHECK(a.str() == b);
    // An unchanged merge (base == mine) gives the file back: what lets a store skip the write.
    CHECK(History::Merge(now, now, now) == now);

    // PERF-01/08: the stamp a store compares before skipping an unchanged transaction.
    {
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        const std::wstring dir = std::wstring(tmp) + L"history-index-test-" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(dir.c_str(), nullptr); const std::wstring path = dir + L"\\history";
        CHECK(ProfileStore::StampOf(path) == ProfileStore::StampOf(path));                 // missing, twice: equal
        CHECK(ProfileStore::Stamp{} != ProfileStore::Stamp{});                              // unknown never equals
        std::ostringstream o; readNew.Write(o);
        CHECK(ProfileStore::WriteAtomic(path, o.str()));
        const auto first = ProfileStore::StampOf(path); CHECK(first.known && first.exists);
        CHECK(ProfileStore::StampOf(path) == first);                                       // untouched: equal
        // A commit with nothing to merge leaves the file (and its stamp) alone.
        History base2 = readNew, mine2 = readNew;
        const auto r = ProfileStore::CommitWith<History>(dir, path, base2, mine2, [](std::istream& in, bool*) { return History::Read(in); },
            [](std::ostream& out, const History& h) { h.Write(out); }, [](const History& b, const History& m, const History& t) { return History::Merge(b, m, t); });
        CHECK(r == ProfileStore::Result::Unchanged && ProfileStore::StampOf(path) == first);
        CHECK(ProfileStore::WriteAtomic(path, o.str(), false));                            // same bytes, rewritten (sidecar mode)
        CHECK(ProfileStore::StampOf(path) != first);                                       // another window's write is seen
        DeleteFileW(path.c_str()); RemoveDirectoryW(dir.c_str());
    }

    std::printf("history_index_test: %u ops + 5000-line read + 5 merges at %zu pages: before %.1f ms, after %.1f ms (x%.0f)\n",
                Ops, History::Limit, tOld, tNew, tNew > 0 ? tOld / tNew : 0.0);
    CHECK(tNew < tOld);
    std::puts("history_index_test OK");
    return 0;
}
