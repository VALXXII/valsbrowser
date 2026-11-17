#pragma once
#include "i18n.h"
// Local password vault (decision 1, option A for NEW saves). INACTIVE until an independent
// security review approves it: while `Vault::Active()` is false nothing here is reached from the
// browser and the engine keeps its own password autosave and suggestions.
//
// Storage: <data>\passwords, one line per account, written only through the profile store
// transaction (src/profile-store.h). The secret is a DPAPI blob (CryptProtectData, current user,
// no UI) whose optional entropy is « browser-vault-v1|<canonical origin> »: a blob copied onto
// another origin's line does not decrypt. Usernames and origins are not secret (shown in B05) and
// are stored base64(UTF-8). No « last used » time is kept (nothing records usage, D3).
// Ids are random 64-bit values, so two windows never mint the same id.
//
// Bridge rules (pure, tested): only https origins (canonical scheme://host[:port]); filling needs a
// user action in our native UI, the target frame and document captured at that action and checked
// again just before injecting (same frame id, same navigation, same origin as the account and as
// the top-level page); a page message is never a reason to read the vault. The value given to the
// page is readable by that page afterwards, like any typed password: nothing pretends otherwise.
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace Vault {

// Flipped only after the independent security review (decision 1). Tests force it per case.
inline bool& ActiveFlag() { static bool active = false; return active; }
// Review §6 (D3): a private window may READ and FILL, never write. Enforced here, in the only write path.
inline bool& WritesForbidden() { static bool forbidden = false; return forbidden; }
inline bool Active() { return ActiveFlag(); }

inline std::string Utf8(const std::wstring& s) { if (s.empty()) return {}; const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr); std::string r(n, 0); WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr); return r; }
inline std::wstring Wide(const std::string& s) { if (s.empty()) return {}; const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0); if (n <= 0) return {}; std::wstring r(n, 0); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), r.data(), n); return r; }
inline std::string Base64(const std::string& bytes) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; unsigned v = 0; int bits = -6;
    for (unsigned char c : bytes) { v = (v << 8) | c; bits += 8; while (bits >= 0) { o.push_back(t[(v >> bits) & 63]); bits -= 6; } }
    if (bits > -6) o.push_back(t[((v << 8) >> (bits + 8)) & 63]);
    while (o.size() % 4) o.push_back('=');
    return o;
}
inline bool Unbase64(const std::string& text, std::string& out) {
    out.clear(); unsigned v = 0; int bits = -8;
    for (char c : text) {
        int d; if (c >= 'A' && c <= 'Z') d = c - 'A'; else if (c >= 'a' && c <= 'z') d = c - 'a' + 26; else if (c >= '0' && c <= '9') d = c - '0' + 52; else if (c == '+') d = 62; else if (c == '/') d = 63; else if (c == '=') break; else return false;
        v = (v << 6) | static_cast<unsigned>(d); bits += 6; if (bits >= 0) { out.push_back(static_cast<char>((v >> bits) & 0xFF)); bits -= 8; }
    }
    return true;
}

// Canonical origin of an https URL: « https://host[:port] », lower-case host, default port
// elided. Empty for anything else (http, file, data, about, user:pass@, no host…).
inline std::wstring CanonicalOrigin(const std::wstring& url) {
    std::wstring u = url; const auto colon = u.find(L"://"); if (colon == std::wstring::npos) return L"";
    std::wstring scheme = u.substr(0, colon); for (auto& c : scheme) c = static_cast<wchar_t>(towlower(c));
    if (scheme != L"https") return L"";
    std::wstring rest = u.substr(colon + 3); const auto end = rest.find_first_of(L"/?#"); std::wstring auth = rest.substr(0, end);
    if (auth.empty() || auth.find(L'@') != std::wstring::npos) return L"";
    std::wstring host = auth, port; const auto pc = auth.rfind(L':');
    if (pc != std::wstring::npos && auth.find(L']') == std::wstring::npos) { host = auth.substr(0, pc); port = auth.substr(pc + 1); }
    if (host.empty()) return L"";
    for (auto& c : host) { c = static_cast<wchar_t>(towlower(c)); if (!(iswalnum(c) || c == L'.' || c == L'-' || c == L'[' || c == L']' || c == L':')) return L""; }
    for (wchar_t c : port) if (!iswdigit(c)) return L"";
    if (port == L"443") port.clear();
    return L"https://" + host + (port.empty() ? L"" : L":" + port);
}

// System crypto resolved at run time from crypt32.dll / bcrypt.dll (System32 only), so every target
// that includes this header (browser, tests, fixtures) needs no extra link library. A missing entry
// point makes the operation fail (nothing saved, nothing revealed).
namespace Sys {
inline HMODULE Load(const wchar_t* dll) { return LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); }
using ProtectFn = BOOL(WINAPI*)(DATA_BLOB*, LPCWSTR, DATA_BLOB*, PVOID, CRYPTPROTECT_PROMPTSTRUCT*, DWORD, DATA_BLOB*);
using UnprotectFn = BOOL(WINAPI*)(DATA_BLOB*, LPWSTR*, DATA_BLOB*, PVOID, CRYPTPROTECT_PROMPTSTRUCT*, DWORD, DATA_BLOB*);
using RandomFn = NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, ULONG);
using HashFn = NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, PUCHAR, ULONG, PUCHAR, ULONG);
inline ProtectFn Protect() { static auto f = reinterpret_cast<ProtectFn>(reinterpret_cast<void*>(GetProcAddress(Load(L"crypt32.dll"), "CryptProtectData"))); return f; }
inline UnprotectFn Unprotect() { static auto f = reinterpret_cast<UnprotectFn>(reinterpret_cast<void*>(GetProcAddress(Load(L"crypt32.dll"), "CryptUnprotectData"))); return f; }
inline RandomFn Random() { static auto f = reinterpret_cast<RandomFn>(reinterpret_cast<void*>(GetProcAddress(Load(L"bcrypt.dll"), "BCryptGenRandom"))); return f; }
inline HashFn Hash() { static auto f = reinterpret_cast<HashFn>(reinterpret_cast<void*>(GetProcAddress(Load(L"bcrypt.dll"), "BCryptHash"))); return f; }
}
inline std::string Entropy(const std::wstring& origin) { return "browser-vault-v1|" + Utf8(origin); }
// DPAPI, bound to the user and to the origin. Empty on failure.
inline std::string Protect(const std::wstring& origin, const std::wstring& secret) {
    std::string plain = Utf8(secret), entropy = Entropy(origin);
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(plain.data())}, ent{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())}, out{};
    std::string blob;
    if (Sys::Protect() && Sys::Protect()(&in, L"browser", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) { blob.assign(reinterpret_cast<char*>(out.pbData), out.cbData); LocalFree(out.pbData); }
    SecureZeroMemory(plain.data(), plain.size());
    return blob;
}
inline bool Unprotect(const std::wstring& origin, const std::string& blob, std::wstring& secret) {
    secret.clear(); std::string entropy = Entropy(origin), copy = blob;
    DATA_BLOB in{static_cast<DWORD>(copy.size()), reinterpret_cast<BYTE*>(copy.data())}, ent{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())}, out{};
    if (!Sys::Unprotect() || !Sys::Unprotect()(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    std::string plain(reinterpret_cast<char*>(out.pbData), out.cbData); SecureZeroMemory(out.pbData, out.cbData); LocalFree(out.pbData);
    secret = Wide(plain); SecureZeroMemory(plain.data(), plain.size());
    return true;
}
// 0 when the system RNG fails (review V8): the caller refuses to save rather than reuse an id.
inline std::uint64_t RandomId() { std::uint64_t v = 0; if (!Sys::Random() || !BCRYPT_SUCCESS(Sys::Random()(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) return 0; return v; }

struct Entry {
    std::uint64_t id = 0; std::wstring origin, user; std::string blob; std::int64_t created = 0, updated = 0;
    bool operator==(const Entry& o) const { return id == o.id && origin == o.origin && user == o.user && blob == o.blob && created == o.created && updated == o.updated; }
};

class Store {
public:
    static constexpr std::size_t Limit = 5000;
    const std::vector<Entry>& Entries() const { return entries_; }
    // Accounts of one origin (no secret), most recently changed first.
    std::vector<Entry> For(const std::wstring& origin) const {
        std::vector<Entry> out; for (const auto& e : entries_) if (e.origin == origin) out.push_back(e);
        std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.updated > b.updated; });
        return out;
    }
    const Entry* Find(std::uint64_t id) const { for (const auto& e : entries_) if (e.id == id) return &e; return nullptr; }
    enum class Saved { Added, Updated, Unchanged, Refused };
    // B04 after consent. Same origin + same username = an update (D1 resolves which account when the
    // form has no username before calling this: never the last one silently).
    Saved Save(const std::wstring& origin, const std::wstring& user, const std::wstring& secret, std::int64_t now) {
        if (origin.empty() || CanonicalOrigin(origin) != origin || secret.empty() || entries_.size() >= Limit) return Saved::Refused;   // review V3: "" is not an origin
        for (auto& e : entries_) if (e.origin == origin && e.user == user) {
            std::wstring old; if (Unprotect(origin, e.blob, old) && old == secret) { SecureZeroMemory(old.data(), old.size() * sizeof(wchar_t)); return Saved::Unchanged; }
            SecureZeroMemory(old.data(), old.size() * sizeof(wchar_t));
            const std::string blob = Protect(origin, secret); if (blob.empty()) return Saved::Refused;
            e.blob = blob; e.updated = now; return Saved::Updated;
        }
        Entry e; e.id = RandomId(); e.origin = origin; e.user = user; e.created = e.updated = now;
        if (!e.id) return Saved::Refused;
        e.blob = Protect(origin, secret);
        if (e.blob.empty()) return Saved::Refused;
        entries_.push_back(std::move(e)); return Saved::Added;
    }
    bool Remove(std::uint64_t id) { const auto n = entries_.size(); entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; }), entries_.end()); return n != entries_.size(); }
    // The secret of `id`, only for the origin it was saved for.
    bool Reveal(std::uint64_t id, const std::wstring& origin, std::wstring& secret) const {
        const Entry* e = Find(id); secret.clear();
        return e && e->origin == origin && Unprotect(origin, e->blob, secret);
    }
    // ---- File format v2 (completeness proof; decision before activation) -------------------------
    //   browser-passwords 2
    //   <id hex>\t<created>\t<updated>\t<b64 origin>\t<b64 user>\t<b64 DPAPI blob>     one line per account
    //   end\t<count>\t<sha256 hex of every byte above this line>
    // Nothing may follow the trailer. An empty vault is the header and « end\t0\t<digest> ». The digest
    // only detects ACCIDENTAL damage (truncation at any record boundary, a lost line, stray bytes): it
    // is not secret and protects nothing against a process of the same user, which could recompute it.
    // Secrets stay protected by DPAPI; origin/frame/document/consent guards stay separate.
    // Version 1 (this lot, never activated) and any other header are refused, never migrated.
    static constexpr const char* Header = "browser-passwords 2";
    static std::string Digest(const std::string& bytes) {
        unsigned char h[32]{};
        if (!Sys::Hash() || !BCRYPT_SUCCESS(Sys::Hash()(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), h, 32))) return {};
        static const char* x = "0123456789abcdef"; std::string o; for (unsigned char c : h) { o += x[c >> 4]; o += x[c & 15]; } return o;
    }
    static bool Digits(const std::string& v) { if (v.empty() || v.size() > 19) return false; for (char c : v) if (c < '0' || c > '9') return false; return true; }
    static bool Hex(const std::string& v) { if (v.empty() || v.size() > 16) return false; for (char c : v) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false; return true; }
    static bool StrictBase64(const std::string& v) { if (v.size() % 4) return false; for (std::size_t i = 0; i < v.size(); ++i) { const char c = v[i]; if (!(isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/' || (c == '=' && i >= v.size() - 2))) return false; } return true; }
    void Write(std::ostream& out) const {
        std::ostringstream body; body << Header << '\n';
        for (const auto& e : entries_) body << std::hex << e.id << std::dec << '\t' << e.created << '\t' << e.updated << '\t' << Base64(Utf8(e.origin)) << '\t' << Base64(Utf8(e.user)) << '\t' << Base64(e.blob) << '\n';
        const std::string b = body.str();
        out << b << "end\t" << entries_.size() << '\t' << Digest(b) << '\n';
    }
    // `damaged`: anything but a complete, consistent v2 file (missing or wrong trailer, count, digest,
    // bytes after the trailer, a bad or duplicate line, too many entries). The caller never writes over
    // a damaged vault (fail closed); what could be read is returned for display only.
    static Store Read(std::istream& in, bool* damaged = nullptr) {
        Store s; if (damaged) *damaged = false;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.empty()) return s;   // absent: a new vault (not damage)
        auto fail = [&]() { if (damaged) *damaged = true; return s; };
        const auto first = text.find('\n');
        if (first == std::string::npos || text.substr(0, first) != Header) return fail();
        const auto endPos = text.rfind("\nend\t");
        if (endPos == std::string::npos) return fail();   // no trailer: truncated
        const std::string body = text.substr(0, endPos + 1), trailer = text.substr(endPos + 1);
        if (trailer.empty() || trailer.back() != '\n' || trailer.find('\n') != trailer.size() - 1) return fail();   // trailer must be the very last line
        const std::string t = trailer.substr(4, trailer.size() - 5); const auto tab = t.find('\t');
        if (tab == std::string::npos) return fail();
        if (!Digits(t.substr(0, tab))) return fail();
        std::uint64_t count = 0; try { count = std::stoull(t.substr(0, tab)); } catch (...) { return fail(); }
        const std::string dg = Digest(body);
        if (count > Limit || dg.empty() || t.substr(tab + 1) != dg) return fail();   // an empty digest (hash failure) never matches
        std::istringstream lines(body.substr(first + 1)); std::string line; bool bad = false;
        while (std::getline(lines, line)) {
            std::vector<std::string> f; std::size_t start = 0;
            for (std::size_t tb; (tb = line.find('\t', start)) != std::string::npos; start = tb + 1) f.push_back(line.substr(start, tb - start));
            f.push_back(line.substr(start));
            Entry e; std::string o, u;
            // Strict fields (review low): exact hex/decimal, canonical base64, valid UTF-8 for a non-empty user.
            if (f.size() != 6 || !Hex(f[0]) || !Digits(f[1]) || !Digits(f[2]) || !StrictBase64(f[3]) || !StrictBase64(f[4]) || !StrictBase64(f[5])) { bad = true; continue; }
            try { e.id = std::stoull(f[0], nullptr, 16); e.created = std::stoll(f[1]); e.updated = std::stoll(f[2]); } catch (...) { bad = true; continue; }   // review V2-L2: an out-of-range number is damage, never a crash
            if (!Unbase64(f[3], o) || !Unbase64(f[4], u) || !Unbase64(f[5], e.blob)) { bad = true; continue; }
            e.origin = Wide(o); e.user = Wide(u);
            if (!u.empty() && e.user.empty()) { bad = true; continue; }
            if (!e.id || e.origin.empty() || CanonicalOrigin(e.origin) != e.origin || e.blob.empty() || s.Find(e.id)) { bad = true; continue; }
            s.entries_.push_back(std::move(e));
        }
        if (bad || s.entries_.size() != count) { if (damaged) *damaged = true; }
        return s;
    }
    bool operator==(const Store& o) const { return entries_ == o.entries_; }
    // Three-way merge by id (profile store): removals here applied, another window's never undone,
    // entries changed here replace the file's, new ones join.
    static Store Merge(const Store& base, const Store& mine, const Store& theirs) {
        Store r;
        for (const auto& t : theirs.entries_) {
            const Entry* b = base.Find(t.id); const Entry* m = mine.Find(t.id);
            if (b && !m) continue;
            if (m && (!b || !(*b == *m))) { r.entries_.push_back(*m); continue; }
            r.entries_.push_back(t);
        }
        for (const auto& m : mine.entries_) {
            const Entry* b = base.Find(m.id);
            if (theirs.Find(m.id)) continue;
            if (!b || !(*b == m)) r.entries_.push_back(m);   // new here, or changed here (an edit outlives a deletion elsewhere, review V6)
        }
        // The same account saved in two windows: keep the most recent one only (review V6).
        std::vector<Entry> out;
        for (const auto& e : r.entries_) {
            auto it = std::find_if(out.begin(), out.end(), [&](const Entry& o) { return o.origin == e.origin && o.user == e.user; });
            if (it == out.end()) out.push_back(e); else if (e.updated > it->updated) *it = e;
        }
        r.entries_ = std::move(out);   // never cut to the limit here (review V2-M2): the commit refuses instead
        return r;
    }
private:
    std::vector<Entry> entries_;
};

// The ONLY way the browser writes the vault (review V2-M3): v2 header only, a damaged or unknown
// file is never rewritten, nothing over the limit is ever written or dropped.
} // namespace Vault
#include "profile-store.h"
namespace Vault {
inline ProfileStore::Result Commit(const std::wstring& dataDir, const std::wstring& path, Store& base, Store& mine, DWORD timeoutMs = 5000) {
    if (WritesForbidden()) return ProfileStore::Result::Failed;   // private window: no Save/Remove reaches the disk (no .tmp, no backup)
    return ProfileStore::Commit<Store>(dataDir, path, base, mine, [](const Store& b, const Store& m, const Store& t) { return Store::Merge(b, m, t); }, timeoutMs,
                                       {Store::Header}, false, [](const Store& s) { return s.Entries().size() <= Store::Limit && !Store::Digest(Store::Header).empty(); });   // V2-L4: no file without a digest
}
// ---- Fill bridge guards -------------------------------------------------------------------------
// What our native UI captured when the user asked to fill (click on the vault control), and what
// the engine reports just before injecting. Coordinates sent by a page are only a placement hint.
struct Target { std::wstring frameSource, topSource; std::uint32_t frameId = 0; std::uint64_t navigation = 0; bool userAction = false; };
enum class Refusal { None, NoUserAction, NotHttps, ThirdPartyFrame, Stale, OtherOrigin };
inline Refusal CanFill(const Target& atAction, const Target& now, const std::wstring& accountOrigin) {
    if (!atAction.userAction) return Refusal::NoUserAction;
    const std::wstring frame = CanonicalOrigin(now.frameSource), top = CanonicalOrigin(now.topSource);
    if (frame.empty() || top.empty()) return Refusal::NotHttps;
    if (frame != top) return Refusal::ThirdPartyFrame;
    if (atAction.frameId != now.frameId || atAction.navigation != now.navigation || CanonicalOrigin(atAction.frameSource) != frame) return Refusal::Stale;
    if (accountOrigin != frame) return Refusal::OtherOrigin;
    return Refusal::None;
}
// Short state for an explicit request in a refused context (D2): never a banner by itself.
inline const wchar_t* RefusalText(Refusal r) {
    switch (r) {
    case Refusal::NotHttps: return TR(vault_refusal_not_https);
    case Refusal::ThirdPartyFrame: return TR(vault_refusal_frame);
    case Refusal::Stale: return TR(vault_refusal_stale);
    case Refusal::OtherOrigin: return TR(vault_refusal_other_origin);
    default: return L"";
    }
}
// JSON string literal (quotes, backslashes, controls, </script>, U+2028/2029 escaped).
inline std::wstring JsonString(const std::wstring& s) {
    std::wstring o = L"\"";
    for (wchar_t c : s) {
        switch (c) { case L'"': o += L"\\\""; break; case L'\\': o += L"\\\\"; break; case L'<': o += L"\\u003c"; break; case L'>': o += L"\\u003e"; break;
        default: if (c < 0x20 || c == 0x2028 || c == 0x2029) { wchar_t b[8]; swprintf(b, 8, L"\\u%04x", static_cast<unsigned>(c)); o += b; } else o += c; }
    }
    return o + L"\"";
}
// Script run in the top-level frame only, in an isolated world of its document (review R-1, see
// IsolatedEvaluateParams below; never ExecuteScript in the page's main world): fills the focused field's form
// (username + password inputs) and returns nothing to the bridge (no secret comes back).
// The script re-checks, at the moment it runs, everything the host checked before injecting (review
// V1/V2): this document's origin and its own (non-opaque) origin equal the account's, a secure
// context, and every ancestor frame of the same origin. It fills only the form holding the focused
// field (no whole-document fallback, review V4) and never a hidden field.
// The script answers ONE fixed status word, never a value of the page or the secret (review 885d322):
// "filled", "origin" (not this exact secure top-level origin), "noform" (the focused element is in no
// form), "nofield" (no visible password field in it). The host says « rempli » only on "filled".
inline std::wstring FillScript(const std::wstring& origin, const std::wstring& user, const std::wstring& secret) {
    return L"(function(o,u,p){if(location.origin!==o||self.origin!==o||!window.isSecureContext)return 'origin';"
           L"var an=location.ancestorOrigins;if(an)for(var i=0;i<an.length;i++)if(an[i]!==o)return 'origin';"
           L"var a=document.activeElement;var f=a&&a.form;if(!f)return 'noform';"
           L"function shown(e){return e&&e.type!=='hidden'&&e.offsetParent!==null&&!e.disabled;}"
           L"var pw=Array.prototype.find.call(f.querySelectorAll('input[type=password]'),shown);if(!pw)return 'nofield';"
           L"var id=Array.prototype.find.call(f.querySelectorAll('input[autocomplete=username],input[type=email],input[type=text]'),shown);"
           L"function set(e,v){var d=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value');d.set.call(e,v);e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));}"
           L"if(id&&u)set(id,u);set(pw,p);return 'filled';})(" + JsonString(origin) + L"," + JsonString(user) + L"," + JsonString(secret) + L");";
}
// What the host may say about the script's answer (the JSON the engine returns): only the fixed words.
enum class FillOutcome { Filled, Origin, NoForm, NoField, Failed };
inline FillOutcome ParseFillResult(long hr, const wchar_t* json) {
    if (hr < 0 || !json) return FillOutcome::Failed;
    const std::wstring j = json;
    return j == L"\"filled\"" ? FillOutcome::Filled : j == L"\"origin\"" ? FillOutcome::Origin : j == L"\"noform\"" ? FillOutcome::NoForm : j == L"\"nofield\"" ? FillOutcome::NoField : FillOutcome::Failed;
}
// ---- Review R-1: the fill runs in an ISOLATED world, never in the page's main world ------------------
// In the main world `self` and `window.origin` are [Replaceable]: an opaque-origin document (CSP sandbox)
// served at the account's URL can make `self.origin` read as that URL's origin and pass FillScript's own
// check. The host therefore evaluates FillScript through the DevTools protocol in a FRESH isolated world of
// the top-level frame (Page.getFrameTree -> Page.createIsolatedWorld -> Runtime.evaluate with that
// contextId). There the globals and prototypes are the world's own, so `self.origin`, `location`,
// `isSecureContext` and the HTMLInputElement setter are the engine's, not the page's: an opaque document reads
// "null" and is refused ("origin"). The world belongs to the document it was created in: a navigation in
// between destroys it and the evaluation fails (nothing filled). No fallback to the main world.
inline bool AsciiIdChar(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9'); }
// Where the value of member `key` of the JSON object opening at `open` starts (members of THAT object only, never of
// a nested one; strings skipped whole, whitespace and any key order accepted). npos when absent or malformed.
inline std::size_t JsonMemberAt(const std::wstring& j, std::size_t open, const wchar_t* key) {
    if (open >= j.size() || j[open] != L'{') return std::wstring::npos;
    int depth = 0;
    for (std::size_t i = open; i < j.size(); ++i) {
        const wchar_t c = j[i];
        if (c == L'"') {
            std::size_t e = i + 1; while (e < j.size() && j[e] != L'"') e += j[e] == L'\\' ? 2 : 1;
            if (e >= j.size()) return std::wstring::npos;
            std::size_t k = e + 1; while (k < j.size() && iswspace(j[k])) ++k;
            if (depth == 1 && k < j.size() && j[k] == L':' && j.compare(i + 1, e - i - 1, key) == 0) { ++k; while (k < j.size() && iswspace(j[k])) ++k; return k < j.size() ? k : std::wstring::npos; }
            i = e; continue;
        }
        if (c == L'{' || c == L'[') ++depth;
        else if ((c == L'}' || c == L']') && --depth == 0) return std::wstring::npos;
    }
    return std::wstring::npos;
}
inline std::size_t JsonRoot(const std::wstring& j) { std::size_t i = 0; while (i < j.size() && iswspace(j[i])) ++i; return i < j.size() && j[i] == L'{' ? i : std::wstring::npos; }
// A plain string value (no escape) starting at `at`; false when it is not one.
inline bool JsonPlainString(const std::wstring& j, std::size_t at, std::wstring& out) {
    out.clear(); if (at >= j.size() || j[at] != L'"') return false;
    const auto e = j.find(L'"', at + 1); if (e == std::wstring::npos) return false;
    out = j.substr(at + 1, e - at - 1); return out.find(L'\\') == std::wstring::npos;
}
// The top-level frame's id in a Page.getFrameTree answer ({frameTree:{frame:{id}}}, never a child frame's);
// "" when absent or not a plain engine id.
inline std::wstring MainFrameId(const std::wstring& json) {
    const auto tree = JsonMemberAt(json, JsonRoot(json), L"frameTree"); const auto frame = JsonMemberAt(json, tree, L"frame");
    std::wstring v; if (!JsonPlainString(json, JsonMemberAt(json, frame, L"id"), v)) return L"";
    if (v.empty() || v.size() > 64) return L"";
    for (wchar_t c : v) if (!AsciiIdChar(c)) return L"";
    return v;
}
inline std::wstring IsolatedWorldParams(const std::wstring& frameId) {
    return L"{\"frameId\":\"" + frameId + L"\",\"worldName\":\"browser-vault-fill\",\"grantUniveralAccess\":false}";   // (sic: the protocol's spelling)
}
// The new world's execution context id from Page.createIsolatedWorld; 0 = refused.
inline long long ContextIdOf(const std::wstring& json) {
    std::size_t at = JsonMemberAt(json, JsonRoot(json), L"executionContextId"); if (at == std::wstring::npos) return 0;
    long long v = 0; std::size_t n = 0;
    while (at < json.size() && json[at] >= L'0' && json[at] <= L'9' && n < 12) { v = v * 10 + (json[at] - L'0'); ++at; ++n; }
    while (at < json.size() && iswspace(json[at])) ++at;
    if (!n || (at < json.size() && json[at] != L'}' && json[at] != L',')) return 0;
    return v;
}
inline std::wstring IsolatedEvaluateParams(long long contextId, const std::wstring& script) {
    return L"{\"expression\":" + JsonString(script) + L",\"contextId\":" + std::to_wstring(contextId) + L",\"returnByValue\":true,\"awaitPromise\":false,\"silent\":true,\"includeCommandLineAPI\":false}";
}
// Runtime.evaluate's answer: only a string result holding one of the fixed words counts; an exception, any
// other type or an engine error is a failure (never « rempli »).
inline FillOutcome ParseIsolatedFillResult(long hr, const wchar_t* json) {
    if (hr < 0 || !json) return FillOutcome::Failed;
    const std::wstring j = json; const auto root = JsonRoot(j);
    if (root == std::wstring::npos || JsonMemberAt(j, root, L"exceptionDetails") != std::wstring::npos) return FillOutcome::Failed;
    const auto result = JsonMemberAt(j, root, L"result"); std::wstring type, value;
    if (!JsonPlainString(j, JsonMemberAt(j, result, L"type"), type) || type != L"string" || !JsonPlainString(j, JsonMemberAt(j, result, L"value"), value)) return FillOutcome::Failed;
    return ParseFillResult(0, (L"\"" + value + L"\"").c_str());
}
inline const wchar_t* FillOutcomeText(FillOutcome o) {
    switch (o) {
    case FillOutcome::Filled: return L"";
    case FillOutcome::Origin: return TR(vault_fill_origin);
    case FillOutcome::NoForm: return TR(vault_fill_no_form);
    case FillOutcome::NoField: return TR(vault_fill_no_field);
    default: return TR(vault_fill_failed);
    }
}
// Review N13: which navigations a pending capture (B04 offer + its secret) survives. Only the SUBMISSION
// itself: the navigation in flight when the capture arrived, else the first one that starts after it, and
// its redirects (same navigation id) while they stay on the capture's origin. Anything else drops the offer:
// another navigation, a redirect or target of another origin (an empty origin, e.g. data: or http, counts as
// another), a failure of the submission. Fail safe: a form posting to another origin (SSO) loses the offer.
class CaptureBinding {
public:
    void Start(const std::wstring& origin, std::uint64_t inFlight) { origin_ = origin; active_ = true; submit_ = inFlight; known_ = inFlight != 0; }
    bool Active() const { return active_; }
    const std::wstring& Origin() const { return origin_; }
    // true: keep the offer; false: drop it (and the binding ends).
    bool NavigationStarting(std::uint64_t id, const std::wstring& targetOrigin) {
        if (!active_) return false;
        if (targetOrigin.empty() || targetOrigin != origin_) return End();
        if (known_) return id == submit_ ? true : End();
        known_ = true; submit_ = id; return true;   // the submission, started after its message arrived
    }
    bool NavigationCompleted(std::uint64_t id, bool ok) {
        if (!active_) return false;
        if (known_ && id == submit_ && !ok) return End();
        return true;
    }
private:
    bool End() { active_ = false; return false; }
    std::wstring origin_; std::uint64_t submit_ = 0; bool known_ = false, active_ = false;
};

} // namespace Vault
