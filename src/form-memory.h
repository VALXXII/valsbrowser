#pragma once
#include "i18n.h"
// #121: our list under a page field (« Infos enregistrées · A », « Liste réunie, comptes + infos · A ») and our
// validation bubble (« Bulle de vérification · A »), Paper page « #121 — Fenêtres du moteur à notre design ».
// Security contract: design/security-review-autofill-under-field.md (P1–P16). This header holds the pure parts,
// unit-tested in tests/form_memory_test.cpp without a window or WebView2:
//   - Store: the e-mails the person typed and sent (P12), our own file <profile>\form-entries, written only
//     through the profile-store transaction; the engine's own form data is never written or deleted (P13);
//   - ReadEngineEmails: the one-time copy of the e-mail values of the engine's « autofill » table (P13), through
//     the private-snapshot reader of src/browser-import.h (never « Login Data », nothing decrypted);
//   - the isolated world « browser-forms » (Script) and what it reports (ParseReport), P3–P10, P14;
//   - rows (BuildRows), placement (Place) and arming (Armed), P2, P7, P11;
//   - the validation messages (InvalidText), P14.
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include "password-vault.h"
#include "browser-import.h"

namespace Forms {

// ---- Values (P12) -------------------------------------------------------------------------------------------
inline std::wstring Lower(const std::wstring& s) { std::wstring o = s; for (auto& c : o) c = static_cast<wchar_t>(towlower(c)); return o; }
inline bool StartsWithI(const std::wstring& s, const std::wstring& prefix) { return prefix.size() <= s.size() && Lower(s.substr(0, prefix.size())) == Lower(prefix); }
inline bool SameI(const std::wstring& a, const std::wstring& b) { return a.size() == b.size() && Lower(a) == Lower(b); }
// No control, line-separator or text-direction character (a page must not write a sentence, or reorder text,
// inside our native UI; same set as the vault's usernames, review V-4).
inline bool PlainChars(const std::wstring& v) {
    for (wchar_t c : v) if (c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0x200E || c == 0x200F || (c >= 0x2028 && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) || c == 0xFEFF) return false;
    return true;
}
// An e-mail as typed: one @, something before it, a domain with a dot that neither starts nor ends it, no space,
// at most 254 characters, plain characters only.
inline bool EmailShaped(const std::wstring& v) {
    if (v.empty() || v.size() > 254 || !PlainChars(v)) return false;
    for (wchar_t c : v) if (iswspace(c)) return false;
    const auto at = v.find(L'@'); if (at == std::wstring::npos || at == 0 || v.find(L'@', at + 1) != std::wstring::npos) return false;
    const std::wstring domain = v.substr(at + 1); const auto dot = domain.find(L'.');
    return !domain.empty() && dot != std::wstring::npos && dot > 0 && domain.back() != L'.' && domain.front() != L'.' && domain.find(L"..") == std::wstring::npos;
}

// The origin a document reports (self.origin) for an http(s) URL: scheme://host[:port], lower case, default port
// elided; "" for anything else (userinfo, other schemes, no host). The bubble works on http pages too (P14); the list
// and the saving need https (P4, Vault::CanonicalOrigin).
inline std::wstring OriginOf(const std::wstring& url) {
    const auto colon = url.find(L"://"); if (colon == std::wstring::npos) return L"";
    const std::wstring scheme = Lower(url.substr(0, colon)); if (scheme != L"https" && scheme != L"http") return L"";
    const std::wstring rest = url.substr(colon + 3); const std::wstring auth = Lower(rest.substr(0, rest.find_first_of(L"/?#")));
    if (auth.empty() || auth.find(L'@') != std::wstring::npos || auth.find(L'\\') != std::wstring::npos) return L"";
    std::wstring host = auth, port; const auto pc = auth.rfind(L':'), bracket = auth.rfind(L']');
    if (pc != std::wstring::npos && (bracket == std::wstring::npos || pc > bracket)) { host = auth.substr(0, pc); port = auth.substr(pc + 1); }
    if (host.empty()) return L"";
    for (wchar_t c : port) if (!iswdigit(c)) return L"";
    if ((scheme == L"https" && port == L"443") || (scheme == L"http" && port == L"80")) port.clear();
    return scheme + L"://" + host + (port.empty() ? L"" : L":" + port);
}

// ---- Store (P12) --------------------------------------------------------------------------------------------
struct Entry {
    std::wstring value; std::int64_t used = 0; std::uint32_t uses = 1;
    bool operator==(const Entry& o) const { return value == o.value && used == o.used && uses == o.uses; }
};
class Store {
public:
    static constexpr std::size_t Limit = 500;
    static constexpr std::uint64_t MaxFileBytes = 1ull << 20;
    //   browser-form-entries 1
    //   <last used, unix ms>\t<uses>\t<b64 UTF-8 value>     one line per e-mail
    //   end\t<count>\t<sha256 hex of every byte above>       (accidental damage only, as the vault's v2)
    static constexpr const char* Header = "browser-form-entries 1";
    const std::vector<Entry>& Entries() const { return entries_; }
    // A value the person typed and sent: added, or its date and count refreshed. The least recently used goes
    // when the store is full (suggestions, not secrets: dropping the oldest is the policy).
    bool Add(const std::wstring& value, std::int64_t now) {
        if (!EmailShaped(value)) return false;
        for (auto& e : entries_) if (SameI(e.value, value)) { e.value = value; e.used = std::max(e.used, now); if (e.uses < 1000000) ++e.uses; return true; }
        Entry e; e.value = value; e.used = now; entries_.push_back(std::move(e)); Trim(); return true;
    }
    // A value picked from our list: only its date (P12: never counted as typed).
    bool Touch(const std::wstring& value, std::int64_t now) { for (auto& e : entries_) if (SameI(e.value, value)) { e.used = std::max(e.used, now); return true; } return false; }
    bool Remove(const std::wstring& value) { const auto n = entries_.size(); entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return SameI(e.value, value); }), entries_.end()); return n != entries_.size(); }
    // Values starting with `prefix` (case-insensitive), most recently used first. An empty prefix matches nothing.
    std::vector<std::wstring> Match(const std::wstring& prefix, std::size_t max) const {
        std::vector<const Entry*> hits; if (prefix.empty()) return {};
        for (const auto& e : entries_) if (StartsWithI(e.value, prefix)) hits.push_back(&e);
        std::stable_sort(hits.begin(), hits.end(), [](const Entry* a, const Entry* b) { return a->used > b->used; });
        std::vector<std::wstring> out; for (const auto* e : hits) { if (out.size() >= max) break; out.push_back(e->value); }
        return out;
    }
    void Write(std::ostream& out) const {
        std::ostringstream body; body << Header << '\n';
        for (const auto& e : entries_) body << e.used << '\t' << e.uses << '\t' << Vault::Base64(Vault::Utf8(e.value)) << '\n';
        const std::string b = body.str();
        out << b << "end\t" << entries_.size() << '\t' << Vault::Store::Digest(b) << '\n';
    }
    // `damaged`: anything but a complete, consistent file. Never rewritten (fail closed, as the vault).
    static Store Read(std::istream& in, bool* damaged = nullptr) {
        Store s; if (damaged) *damaged = false;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.empty()) return s;
        auto fail = [&]() { if (damaged) *damaged = true; return Store{}; };
        if (text.size() > MaxFileBytes) return fail();
        const auto first = text.find('\n');
        if (first == std::string::npos || text.substr(0, first) != Header) return fail();
        const auto endPos = text.rfind("\nend\t"); if (endPos == std::string::npos) return fail();
        const std::string body = text.substr(0, endPos + 1), trailer = text.substr(endPos + 1);
        if (trailer.empty() || trailer.back() != '\n' || trailer.find('\n') != trailer.size() - 1) return fail();
        const std::string t = trailer.substr(4, trailer.size() - 5); const auto tab = t.find('\t'); if (tab == std::string::npos) return fail();
        if (!Vault::Store::Digits(t.substr(0, tab))) return fail();
        std::uint64_t count = 0; try { count = std::stoull(t.substr(0, tab)); } catch (...) { return fail(); }
        const std::string dg = Vault::Store::Digest(body);
        if (count > Limit || dg.empty() || t.substr(tab + 1) != dg) return fail();
        std::istringstream lines(body.substr(first + 1)); std::string line; bool bad = false;
        while (std::getline(lines, line)) {
            std::vector<std::string> f; std::size_t start = 0;
            for (std::size_t tb; (tb = line.find('\t', start)) != std::string::npos; start = tb + 1) f.push_back(line.substr(start, tb - start));
            f.push_back(line.substr(start));
            Entry e; std::string v;
            if (f.size() != 3 || !Vault::Store::Digits(f[0]) || !Vault::Store::Digits(f[1]) || f[1].size() > 7 || !Vault::Store::StrictBase64(f[2]) || !Vault::Unbase64(f[2], v)) { bad = true; continue; }
            try { e.used = std::stoll(f[0]); e.uses = static_cast<std::uint32_t>(std::stoul(f[1])); } catch (...) { bad = true; continue; }
            e.value = Vault::Wide(v);
            if (!EmailShaped(e.value) || std::any_of(s.entries_.begin(), s.entries_.end(), [&](const Entry& o) { return SameI(o.value, e.value); })) { bad = true; continue; }
            s.entries_.push_back(std::move(e));
        }
        if (bad || s.entries_.size() != count) { if (damaged) *damaged = true; }
        return s;
    }
    bool operator==(const Store& o) const { return entries_ == o.entries_; }
    // Three-way merge by value (case-insensitive): a removal here is applied, another window's removal is never
    // undone (« retirer » wins over a later use elsewhere), what changed here replaces the file's, new ones join.
    static Store Merge(const Store& base, const Store& mine, const Store& theirs) {
        auto find = [](const Store& s, const std::wstring& v) -> const Entry* { for (const auto& e : s.entries_) if (SameI(e.value, v)) return &e; return nullptr; };
        Store r;
        for (const auto& t : theirs.entries_) {
            const Entry* b = find(base, t.value); const Entry* m = find(mine, t.value);
            if (b && !m) continue;                                   // removed here
            if (m && (!b || !(*b == *m))) { Entry x = *m; x.used = std::max(m->used, t.used); x.uses = std::max(m->uses, t.uses); r.entries_.push_back(x); continue; }
            r.entries_.push_back(t);
        }
        for (const auto& m : mine.entries_) {
            if (find(theirs, m.value)) continue;
            if (!find(base, m.value)) r.entries_.push_back(m);      // new here; one removed elsewhere stays removed
        }
        r.Trim(); return r;
    }
private:
    void Trim() {
        if (entries_.size() <= Limit) return;
        std::stable_sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.used > b.used; });
        entries_.resize(Limit);
    }
    std::vector<Entry> entries_;
};
// Review §6 / P11: a private window never writes; enforced in the only write path.
inline bool& WritesForbidden() { static bool forbidden = false; return forbidden; }
inline ProfileStore::Result Commit(const std::wstring& dataDir, const std::wstring& path, Store& base, Store& mine, DWORD timeoutMs = 2000) {
    if (WritesForbidden()) return ProfileStore::Result::Failed;
    return ProfileStore::Commit<Store>(dataDir, path, base, mine, [](const Store& b, const Store& m, const Store& t) { return Store::Merge(b, m, t); }, timeoutMs,
                                       {Store::Header}, false, [](const Store& s) { return s.Entries().size() <= Store::Limit && !Vault::Store::Digest(Store::Header).empty(); });
}
// « Effacer les données » (clear on exit, full clear): the file replaced by an empty store, under the lock.
inline bool Wipe(const std::wstring& dataDir, const std::wstring& path, Store& base, Store& mine) {
    if (WritesForbidden()) return false;
    ProfileStore::Lock lock(dataDir, 2000); if (!lock.Held()) return false;
    std::ostringstream o; Store{}.Write(o);
    if (!ProfileStore::WriteAtomic(path, o.str())) return false;
    base = Store{}; mine = Store{}; return true;
}

// ---- One-time copy of the engine's typed e-mails (P13) ----------------------------------------------------------
// `webData`: <engine user data>\EBWebView\Default\Web Data. The source is copied (never opened in place, never
// written) by BrowserImport::Snapshot, which opens it WITHOUT write sharing: while the engine runs it holds the file
// and the copy fails (Busy, retried at the next start). Only the « autofill » table, only e-mail-shaped values.
enum class EngineImport { Copied, Nothing, Busy, Failed };
struct EngineEmail { std::wstring value; std::int64_t usedMs = 0; };
inline EngineImport ReadEngineEmails(const std::wstring& webData, std::vector<EngineEmail>& out, const std::wstring& tempRoot = L"") {
    out.clear();
    if (GetFileAttributesW(webData.c_str()) == INVALID_FILE_ATTRIBUTES) return EngineImport::Nothing;
    BrowserImport::Snapshot snap; const auto f = snap.Take(webData, tempRoot);
    if (f == BrowserImport::Failure::Busy) return EngineImport::Busy;
    if (f == BrowserImport::Failure::Missing) return EngineImport::Nothing;
    if (f != BrowserImport::Failure::None) return EngineImport::Failed;
    BrowserImport::Database db(snap.Path()); if (db.failure() != BrowserImport::Failure::None) return EngineImport::Failed;
    if (!BrowserImport::Sql::PlainTables(db.handle(), "'autofill'", 1)) return EngineImport::Nothing;   // no such table: nothing typed
    BrowserImport::Sql::Statement q(db.handle(), "SELECT value, date_last_used FROM autofill ORDER BY date_last_used DESC LIMIT 20000");
    if (!q.Prepared()) return EngineImport::Failed;
    int rc;
    while ((rc = q.Step()) == BrowserImport::Sql::ROW) {
        std::wstring v = q.Text(0); while (!v.empty() && iswspace(v.back())) v.pop_back(); while (!v.empty() && iswspace(v.front())) v.erase(v.begin());
        if (!EmailShaped(v)) continue;
        if (std::any_of(out.begin(), out.end(), [&](const EngineEmail& e) { return SameI(e.value, v); })) continue;
        const std::int64_t seconds = q.Int(1);
        out.push_back({v, seconds > 0 && seconds < 32503680000LL ? seconds * 1000 : 0});
        if (out.size() >= Store::Limit) break;
    }
    if (rc != BrowserImport::Sql::ROW && rc != BrowserImport::Sql::DONE && out.empty()) return EngineImport::Failed;
    return out.empty() ? EngineImport::Nothing : EngineImport::Copied;
}

// ---- What the isolated world reports (P3–P10, P14) --------------------------------------------------------------
inline constexpr const wchar_t* World = L"browser-forms";
inline constexpr const wchar_t* BindingName = L"browserForms";
struct Report {
    enum class Kind { Type, Select, Pick, Remove, Close, Save, Invalid, Unbubble } kind = Kind::Close;
    std::wstring origin, value, field, message, argA, argB;
    std::vector<std::wstring> values;   // Save
    int seq = 0, index = -1; bool acOff = false, password = false;
    double x = 0, y = 0, w = 0, h = 0, dpr = 1, vw = 0, vh = 0;
};
// A short decimal number written by the world (String(n)); finite and bounded, else refused.
inline bool Number(const std::wstring& s, double& out, double lo, double hi) {
    if (s.empty() || s.size() > 24) return false;
    for (wchar_t c : s) if (!(iswdigit(c) || c == L'.' || c == L'-' || c == L'e' || c == L'E' || c == L'+')) return false;
    wchar_t* end = nullptr; const double v = std::wcstod(s.c_str(), &end);
    if (!end || *end || !(v >= lo && v <= hi)) return false;
    out = v; return true;
}
// Runtime.bindingCalled of OUR binding: {name, payload, executionContextId}. The context id is the engine's (the
// world that called), never a value of the page. Every member of the payload is a bounded string.
inline bool ParseReport(const std::wstring& event, Report& out, long long& context) {
    out = Report{}; context = 0;
    if (event.size() > 16384) return false;
    const auto root = Vault::JsonRoot(event); std::wstring name, payload;
    if (!Vault::JsonStringAt(event, Vault::JsonMemberAt(event, root, L"name"), name) || name != BindingName) return false;
    if (!Vault::JsonStringAt(event, Vault::JsonMemberAt(event, root, L"payload"), payload) || payload.size() > 8192) return false;
    {   const auto at = Vault::JsonMemberAt(event, root, L"executionContextId"); if (at == std::wstring::npos) return false;
        long long v = 0; std::size_t i = at, n = 0; while (i < event.size() && iswdigit(event[i]) && n < 12) { v = v * 10 + (event[i] - L'0'); ++i; ++n; }
        if (!n || v <= 0) return false;
        context = v; }
    const auto p = Vault::JsonRoot(payload); if (p == std::wstring::npos) return false;
    auto str = [&](const wchar_t* key, std::wstring& v) { return Vault::JsonStringAt(payload, Vault::JsonMemberAt(payload, p, key), v); };
    std::wstring k, s; if (!str(L"k", k) || !str(L"o", out.origin) || out.origin.size() > 300) return false;
    auto seq = [&]() { double v = 0; std::wstring t; if (!str(L"s", t) || !Number(t, v, 1, 1e9) || v != static_cast<int>(v)) return false; out.seq = static_cast<int>(v); return true; };
    auto num = [&](const wchar_t* key, double& v, double lo, double hi) { std::wstring t; return str(key, t) && Number(t, v, lo, hi); };
    auto geometry = [&]() { return num(L"x", out.x, -1e5, 1e5) && num(L"y", out.y, -1e5, 1e5) && num(L"w", out.w, 0, 1e5) && num(L"h", out.h, 0, 1e5) && num(L"d", out.dpr, .25, 16) && num(L"vw", out.vw, 1, 1e5) && num(L"vh", out.vh, 1, 1e5); };
    if (k == L"type") {
        out.kind = Report::Kind::Type; std::wstring ac, pw;
        if (!seq() || !str(L"v", out.value) || out.value.size() > 256 || !str(L"f", out.field) || (out.field != L"email" && out.field != L"id") || !geometry()) return false;
        str(L"ac", ac); str(L"pw", pw); out.acOff = ac == L"off"; out.password = pw == L"1";
        return true;
    }
    if (k == L"sel" || k == L"pick" || k == L"del") {
        out.kind = k == L"sel" ? Report::Kind::Select : k == L"pick" ? Report::Kind::Pick : Report::Kind::Remove; double i = -1;
        if (!seq() || !num(L"i", i, 0, 63) || i != static_cast<int>(i)) return false;
        out.index = static_cast<int>(i); return true;
    }
    if (k == L"close") { out.kind = Report::Kind::Close; return seq(); }
    if (k == L"unbubble") { out.kind = Report::Kind::Unbubble; return true; }
    if (k == L"save") {
        out.kind = Report::Kind::Save; std::wstring all; if (!str(L"v", all) || all.size() > 5 * 257) return false;
        std::wstring cur; for (wchar_t c : all + L"\n") { if (c == L'\n') { if (!cur.empty() && out.values.size() < 5) out.values.push_back(cur); cur.clear(); } else cur += c; }
        return !out.values.empty();
    }
    if (k == L"invalid") {
        out.kind = Report::Kind::Invalid;
        if (!str(L"m", out.message) || out.message.size() > 32 || !geometry()) return false;
        str(L"a", out.argA); str(L"b", out.argB);
        if (out.argA.size() > 300 || out.argB.size() > 32) return false;
        return true;
    }
    return false;
}

// ---- Rows (P11) -------------------------------------------------------------------------------------------------
struct AccountRow { std::uint64_t id = 0; std::wstring user; };
struct Rows {
    std::vector<AccountRow> accounts; std::vector<std::wstring> infos; std::size_t typed = 0;   // `typed`: letters shown bold in the infos
    bool Empty() const { return accounts.empty() && infos.empty(); }
    std::size_t Count() const { return accounts.size() + infos.size(); }
};
inline constexpr std::size_t MaxAccounts = 4, MaxInfos = 5;
// `siteAccounts`: the vault's entries for the EXACT origin (no secret: id and username only). An e-mail that is
// already one of the site's accounts is never repeated in the infos (board « Liste réunie », « sans doublon »).
inline Rows BuildRows(const std::vector<AccountRow>& siteAccounts, const Store* infos, const std::wstring& typed, bool showAccounts, bool showInfos) {
    Rows r; if (typed.empty()) return r;
    r.typed = typed.size();
    if (showAccounts) for (const auto& a : siteAccounts) if (!a.user.empty() && StartsWithI(a.user, typed) && r.accounts.size() < MaxAccounts) r.accounts.push_back(a);
    if (showInfos && infos) for (const auto& v : infos->Match(typed, MaxInfos + MaxAccounts + 1)) {
        if (r.infos.size() >= MaxInfos) break;
        if (SameI(v, typed)) continue;   // nothing left to complete
        if (std::any_of(siteAccounts.begin(), siteAccounts.end(), [&](const AccountRow& a) { return SameI(a.user, v); })) continue;
        r.infos.push_back(v);
    }
    return r;
}

// ---- Placement (P2) and arming (P7) -----------------------------------------------------------------------------
// Screen pixels. `field` and `page` (the tab's own WebView rectangle). Under the field `gap` px, left edges aligned,
// moved left to stay inside the page; above the field when there is no room below; false when it fits nowhere or the
// field is not inside the page.
inline bool Place(const RECT& field, const RECT& page, int width, int height, int gap, RECT& out) {
    if (width <= 0 || height <= 0 || page.right - page.left < width) return false;
    if (field.left < page.left || field.top < page.top || field.right > page.right || field.bottom > page.bottom || field.right <= field.left || field.bottom <= field.top) return false;
    int left = field.left; if (left + width > page.right) left = page.right - width; if (left < page.left) left = page.left;
    int top = field.bottom + gap;
    if (top + height > page.bottom) { top = field.top - gap - height; if (top < page.top) return false; }
    out = {left, top, left + width, top + height}; return true;
}
// CSS px of the top document (the world's getBoundingClientRect) to screen px: `view` is the WebView's screen
// rectangle, `dpr` its devicePixelRatio (zoom × DPI).
inline RECT FieldScreen(const Report& r, const RECT& view) {
    auto px = [&](double v) { return static_cast<LONG>(std::lround(v * r.dpr)); };
    return {view.left + px(r.x), view.top + px(r.y), view.left + px(r.x + r.w), view.top + px(r.y + r.h)};
}
inline constexpr ULONGLONG ArmMs = 500;
inline bool Armed(ULONGLONG shownAt, ULONGLONG now, bool foreground) { return shownAt && foreground && now >= shownAt + ArmMs; }

// ---- Validation messages (P14) -----------------------------------------------------------------------------------
// The page's own setCustomValidity text: one line, at most 150 characters, plain characters only; "" = nothing to say.
inline std::wstring CustomText(const std::wstring& raw) {
    std::wstring o; for (wchar_t c : raw) { if (c == L'\n' || c == L'\r' || c == L'\t') c = L' '; if (!PlainChars(std::wstring(1, c))) continue; if (c == L' ' && (o.empty() || o.back() == L' ')) continue; o += c; }
    while (!o.empty() && o.back() == L' ') o.pop_back();
    if (o.size() > 150) { o.resize(149); o += L'\x2026'; }
    return o;
}
// A limit the engine reports (minlength, min, max): digits and the separators of numbers, dates and times only.
inline bool LimitArg(const std::wstring& v) {
    if (v.empty() || v.size() > 32) return false;
    for (wchar_t c : v) if (!(iswdigit(c) || c == L'.' || c == L'-' || c == L':' || c == L'T' || c == L'W' || c == L',')) return false;
    return true;
}
// Our words for each validity state (the board shows « Il manque le @ dans l'adresse e-mail. »; the other states
// follow the same tone). "" = nothing shown (the engine's bubble is still cancelled: never two bubbles).
inline std::wstring InvalidText(const std::wstring& kind, const std::wstring& a, const std::wstring& b) {
    if (kind == L"email-at") return TR(form_invalid_email_at);
    if (kind == L"email-local") return TR(form_invalid_email_local);
    if (kind == L"email-domain") return TR(form_invalid_email_domain);
    if (kind == L"email") return TR(form_invalid_email);
    if (kind == L"url") return TR(form_invalid_url);
    if (kind == L"missing") return TR(form_missing);
    if (kind == L"missing-checkbox") return TR(form_missing_checkbox);
    if (kind == L"missing-choice") return TR(form_missing_choice);
    if (kind == L"missing-file") return TR(form_missing_file);
    if (kind == L"missing-value") return TR(form_missing_value);   // owner decision: a required date, time, colour or range left empty
    if (kind == L"bad-date") return TR(form_bad_date);
    if (kind == L"bad-time") return TR(form_bad_time);
    if (kind == L"pattern") return TR(form_pattern);
    if (kind == L"bad-number") return TR(form_bad_number);
    if (kind == L"bad-input" || kind == L"step") return TR(form_bad_input);
    auto count = [](const std::wstring& v, long long& n) { if (v.empty() || v.size() > 9) return false; n = 0; for (wchar_t c : v) { if (!iswdigit(c)) return false; n = n * 10 + (c - L'0'); } return true; };
    long long n = 0, now = 0;
    if (kind == L"too-short" && count(a, n) && count(b, now)) return TRP(form_too_short, n, std::to_wstring(now));
    if (kind == L"too-long" && count(a, n) && count(b, now)) return TRP(form_too_long, n, std::to_wstring(now));
    if (kind == L"underflow" && LimitArg(a)) return TRF(form_underflow, a);
    if (kind == L"overflow" && LimitArg(a)) return TRF(form_overflow, a);
    if (kind == L"custom") { const auto t = CustomText(a); return t.empty() ? std::wstring(TR(form_bad_input)) : t; }
    return TR(form_bad_input);
}

// ---- The isolated world (P3–P10, P14) ----------------------------------------------------------------------------
// Installed with Page.addScriptToEvaluateOnNewDocument in the world « browser-forms » of the tab's OWN session (top
// document only; frames keep the engine's bubble and get no list), before any page script. Its globals, prototypes
// and listeners are the world's own: the page cannot read them, call the binding, or forge a trusted event.
// It reports (binding browserForms, only in this world):
//   type    : a trusted key then a trusted input in an identifier field of the top document, which passes P5;
//   sel/pick/del: ↓ ↑ Entrée Suppr on that field while the host has a list for it (host told it the row count: shown());
//   close   : blur, scroll, resize, the field moved, Échap, Tab, Entrée without a selected row, page hidden;
//   save    : e-mails the person typed (P12) at a trusted submit, click on a button-like element or Entrée;
//   invalid : the first invalid control of a validation pass the page did not handle itself; unbubble when it ends.
// The host calls back only through Runtime.evaluate in the context id of the report (__bf.shown/closed/fill).
inline std::wstring Script() {
    return std::wstring(L"(function(){'use strict';if(window.top!==window)return;"
        L"var O=self.origin;if(O==='null'||location.origin!==O||(location.protocol!=='https:'&&location.protocol!=='http:'))return;"
        L"function send(m){m.o=O;var s;try{s=JSON.stringify(m);}catch(x){return;}var n=0;(function go(){var f=window.") + BindingName + L";if(typeof f==='function'){try{f(s);}catch(x){}return;}if(++n<15)setTimeout(go,200);})();}"
        L"function low(v){return String(v||'').toLowerCase();}function ac(e){return low(e.getAttribute('autocomplete'));}"
        L"function p0(e){var p=e.composedPath?e.composedPath():null;return p&&p.length?p[0]:e.target;}"
        // P5: an identifier field (e-mail, or text with an identifier hint), never a password.
        L"function kind(e){if(!e||e.tagName!=='INPUT')return '';var t=low(e.type||'text');if(t==='email')return 'email';if(t!=='text')return '';"
        L"if(/(^|\\s)(username|email)(\\s|$)/.test(ac(e))||/mail|login|user|ident|account|compte/i.test(String(e.name||'')+' '+String(e.id||'')))return 'id';return '';}"
        L"function deep(){var a=document.activeElement;while(a&&a.shadowRoot&&a.shadowRoot.activeElement)a=a.shadowRoot.activeElement;return a;}"
        L"function opac(e){var o=1;for(var n=e;n&&n.nodeType===1;n=n.parentElement||(n.parentNode&&n.parentNode.host)||null){var v=parseFloat(getComputedStyle(n).opacity);if(v>=0)o*=v;}return o;}"
        // P5: connected, enabled, writable, visible, opaque enough, whole and not covered, no pinch zoom.
        L"function rectOf(e){if(!e||!e.isConnected||e.disabled||e.readOnly)return null;"
        L"if(e.checkVisibility&&!e.checkVisibility({opacityProperty:true,visibilityProperty:true,contentVisibilityAuto:true}))return null;"
        L"if(opac(e)<0.5)return null;var vv=window.visualViewport;if(vv&&Math.abs(vv.scale-1)>0.001)return null;"
        L"var r=e.getBoundingClientRect();if(r.width<8||r.height<8||r.left<0||r.top<0||r.right>innerWidth||r.bottom>innerHeight)return null;"
        L"var root=e.getRootNode?e.getRootNode():document;var h=(root&&root.elementFromPoint?root:document).elementFromPoint(r.left+r.width/2,r.top+r.height/2);"
        L"if(!h||!(h===e||e.contains(h)))return null;return r;}"
        // FillScript's own test of a password field (password-vault.h): same answer as the B03 fill will give.
        L"function fillShown(e){if(!e||e.type==='hidden'||e.disabled||e.offsetParent===null)return false;"
        L"if(e.checkVisibility&&!e.checkVisibility({opacityProperty:true,visibilityProperty:true,contentVisibilityAuto:true}))return false;"
        L"var r=e.getBoundingClientRect();if(r.width<2||r.height<2||r.right<=0||r.bottom<=0||r.left>=innerWidth||r.top>=innerHeight)return false;"
        L"var h=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);return !!h&&(h===e||e.contains(h)||h.control===e);}"
        L"function geo(m,r){m.x=String(r.left);m.y=String(r.top);m.w=String(r.width);m.h=String(r.height);m.d=String(window.devicePixelRatio||1);m.vw=String(innerWidth);m.vh=String(innerHeight);return m;}"
        L"var lastKey=-1e9,keyEl=null,act=-1e9,cur=null,cr=null,seq=0,open=0,sel=-1,typed=new WeakMap(),watching=false,bub=null,bubAt=0,pass=false;"
        L"function close(){var had=!!cur;cur=null;cr=null;open=0;sel=-1;if(had)send({k:'close',s:String(seq)});}"
        L"function same(a,b,t){return Math.abs(a.left-b.left)<=t&&Math.abs(a.top-b.top)<=t&&Math.abs(a.width-b.width)<=t&&Math.abs(a.height-b.height)<=t;}"
        // P8: the field moved, lost the focus or left the document: the list closes.
        L"function watch(){if(watching)return;watching=true;(function tick(){if(!cur){watching=false;return;}var r=cur.isConnected&&cur===deep()?cur.getBoundingClientRect():null;"
        L"if(!r||!same(r,cr,1)){watching=false;close();return;}requestAnimationFrame(tick);})();}"
        L"function unbubble(t){if(bub&&(!t||t===bub)){bub=null;send({k:'unbubble'});}}"
        // P12: e-mails the person typed (the value is still the last one typed), visible, not autocomplete=off / one-time-code.
        L"function emails(scope){var out=[];Array.prototype.forEach.call(scope.querySelectorAll('input'),function(e){if(!kind(e)||!e.value||typed.get(e)!==e.value)return;"
        L"var a=ac(e);if(a.indexOf('off')===0||a.indexOf('one-time-code')>=0||!e.isConnected||e.getClientRects().length===0)return;"
        L"var v=String(e.value).slice(0,256);if(out.indexOf(v)<0&&out.length<5)out.push(v);});return out;}"
        L"function save(scope){if(!scope||!scope.querySelectorAll||performance.now()-act>10000)return;var v=emails(scope);if(v.length)send({k:'save',v:v.join('\\n')});}"
        // Listeners on WINDOW, capture phase, installed before any page script: they run first.
        L"window.addEventListener('keydown',function(e){if(!e.isTrusted)return;act=performance.now();var t=p0(e);"
        L"if(open&&t===cur){var k=e.key;"
        L"if(k==='ArrowDown'||k==='ArrowUp'){e.preventDefault();e.stopImmediatePropagation();sel=k==='ArrowDown'?(sel+1)%open:(sel<=0?open-1:sel-1);send({k:'sel',s:String(seq),i:String(sel)});return;}"
        L"if(k==='Enter'&&sel>=0&&!e.repeat&&!e.isComposing){e.preventDefault();e.stopImmediatePropagation();send({k:'pick',s:String(seq),i:String(sel)});return;}"
        L"if(k==='Delete'&&sel>=0&&!e.repeat){e.preventDefault();e.stopImmediatePropagation();send({k:'del',s:String(seq),i:String(sel)});return;}"
        L"if(k==='Escape'){e.preventDefault();e.stopImmediatePropagation();close();return;}"
        L"if(k==='Tab'||k==='Enter')close();}"
        L"if(e.key==='Enter'&&t&&t.tagName==='INPUT')save(t.form||document);"
        L"lastKey=performance.now();keyEl=t;},true);"
        // P3: a trusted key on this very field, then its trusted input, within 1 s (execCommand has no key).
        L"window.addEventListener('input',function(e){if(!e.isTrusted)return;var t=p0(e);if(!t||t.tagName!=='INPUT')return;typed.set(t,t.value);unbubble(t);"
        L"var k=kind(t);if(!k){if(cur)close();return;}if(t!==keyEl||performance.now()-lastKey>1000)return;"
        L"var r=rectOf(t);if(!r||t!==deep()){if(cur)close();return;}"
        L"cur=t;cr=r;seq++;open=0;sel=-1;var f=t.form,pw=false;if(f)pw=Array.prototype.some.call(f.querySelectorAll('input[type=password]'),fillShown);"
        L"send(geo({k:'type',s:String(seq),v:String(t.value).slice(0,256),f:k,ac:ac(t).indexOf('off')===0?'off':'',pw:pw?'1':'0'},r));watch();},true);"
        L"window.addEventListener('focusout',function(e){var t=p0(e);if(cur&&t===cur)close();if(bub&&t===bub)unbubble();},true);"
        L"window.addEventListener('scroll',function(){if(cur)close();if(bub&&performance.now()-bubAt>300)unbubble();},true);"
        L"window.addEventListener('resize',function(){if(cur)close();unbubble();},true);"
        L"window.addEventListener('pagehide',function(){if(cur)close();unbubble();},true);"
        L"window.addEventListener('submit',function(e){if(e.isTrusted&&e.target)save(e.target);},true);"
        L"window.addEventListener('click',function(e){if(!e.isTrusted)return;act=performance.now();var t=p0(e),b=t&&t.closest?t.closest('button,input[type=submit],input[type=button],input[type=image],[role=button]'):null;"
        L"if(b)save(b.form||(b.closest&&b.closest('form'))||document);},true);"
        // P14: the validity state (the page's own message only for setCustomValidity, cut by the host).
        L"function vkind(e){var v=e.validity;if(!v)return null;var t=low(e.type||e.tagName);"
        L"if(v.customError)return ['custom',String(e.validationMessage||'').slice(0,300)];"
        L"if(v.valueMissing){if(t==='checkbox')return ['missing-checkbox'];if(t==='radio'||e.tagName==='SELECT')return ['missing-choice'];if(t==='file')return ['missing-file'];"
        L"if(/^(date|datetime-local|month|week|time|color|range)$/.test(t))return ['missing-value'];return ['missing'];}"
        L"if(v.badInput)return [t==='number'?'bad-number':/^(date|datetime-local|month|week)$/.test(t)?'bad-date':t==='time'?'bad-time':'bad-input'];"
        L"if(v.typeMismatch){if(t==='email'){var s=String(e.value),i=s.indexOf('@');if(i<0)return ['email-at'];if(i===0)return ['email-local'];if(i===s.length-1)return ['email-domain'];return ['email'];}if(t==='url')return ['url'];return ['bad-input'];}"
        L"if(v.patternMismatch)return ['pattern'];"
        L"if(v.tooLong)return ['too-long',String(e.maxLength),String(String(e.value).length)];"
        L"if(v.tooShort)return ['too-short',String(e.minLength),String(String(e.value).length)];"
        L"if(v.rangeUnderflow)return ['underflow',String(e.min)];if(v.rangeOverflow)return ['overflow',String(e.max)];"
        L"if(v.stepMismatch)return ['step'];return null;}"
        // P14: a listener appended to the target during the dispatch runs last: only when the page did not cancel it.
        L"window.addEventListener('invalid',function(e){if(!e.isTrusted)return;var t=e.target;if(!t||!t.addEventListener)return;"
        L"var last=function(ev){t.removeEventListener('invalid',last);if(ev.defaultPrevented)return;ev.preventDefault();"
        L"if(pass)return;pass=true;setTimeout(function(){pass=false;},0);"
        L"try{t.focus();}catch(x){}if(t.scrollIntoViewIfNeeded)t.scrollIntoViewIfNeeded(false);"
        L"var k=vkind(t),r=t.getBoundingClientRect();if(!k||r.width<=0||r.height<=0||r.bottom<0||r.top>innerHeight||r.right<0||r.left>innerWidth)return;"
        L"bub=t;bubAt=performance.now();send(geo({k:'invalid',m:k[0],a:k[1]||'',b:k[2]||''},r));};"
        L"t.addEventListener('invalid',last);},true);"
        // The host's calls (Runtime.evaluate in this context only). fill: P9, the SAME element, still focused, P5, not moved.
        L"window.__bf={shown:function(s,n){if(s===seq&&cur){open=Math.max(0,Math.min(40,n|0));sel=-1;}return 'ok';},"
        L"closed:function(s){if(s===seq){open=0;sel=-1;}return 'ok';},"
        L"fill:function(s,v){if(s!==seq||!cur)return 'stale';var e=cur;if(e!==deep())return 'stale';var r=rectOf(e);if(!r||!same(r,cr,2))return 'moved';"
        L"var d=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value');d.set.call(e,String(v));"
        L"e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));cur=null;cr=null;open=0;sel=-1;return 'filled';}};"
        L"})();";
}
inline std::wstring ScriptParams() { return L"{\"source\":" + Vault::JsonString(Script()) + L",\"worldName\":\"" + World + L"\"}"; }
inline std::wstring BindingParams() { return std::wstring(L"{\"name\":\"") + BindingName + L"\",\"executionContextName\":\"" + World + L"\"}"; }
// The host's calls into the world (numbers and one JSON string literal only).
inline std::wstring ShownCall(int seq, std::size_t rows) { return L"__bf.shown(" + std::to_wstring(seq) + L"," + std::to_wstring(rows) + L")"; }
inline std::wstring ClosedCall(int seq) { return L"__bf.closed(" + std::to_wstring(seq) + L")"; }
inline std::wstring FillCall(int seq, const std::wstring& value) { return L"__bf.fill(" + std::to_wstring(seq) + L"," + Vault::JsonString(value) + L")"; }
inline std::wstring EvaluateParams(long long context, const std::wstring& expression) { return Vault::IsolatedEvaluateParams(context, expression); }
// Runtime.evaluate's answer to fill(): « filled » only from a string result holding exactly that word.
enum class FillAnswer { Filled, Stale, Moved, Failed };
inline FillAnswer ParseFillAnswer(long hr, const wchar_t* json) {
    if (hr < 0 || !json) return FillAnswer::Failed;
    const std::wstring j = json; const auto root = Vault::JsonRoot(j);
    if (root == std::wstring::npos || Vault::JsonMemberAt(j, root, L"exceptionDetails") != std::wstring::npos) return FillAnswer::Failed;
    const auto result = Vault::JsonMemberAt(j, root, L"result"); std::wstring type, value;
    if (!Vault::JsonPlainString(j, Vault::JsonMemberAt(j, result, L"type"), type) || type != L"string" || !Vault::JsonPlainString(j, Vault::JsonMemberAt(j, result, L"value"), value)) return FillAnswer::Failed;
    return value == L"filled" ? FillAnswer::Filled : value == L"stale" ? FillAnswer::Stale : value == L"moved" ? FillAnswer::Moved : FillAnswer::Failed;
}

// ---- Engine autofill (P13) ---------------------------------------------------------------------------------------
// The engine's own list never shows: IsGeneralAutofillEnabled is FALSE in every window while our forms run.
inline constexpr bool EngineAutofill = false;

} // namespace Forms
