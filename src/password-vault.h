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
           L"function shown(e){if(!e||e.type==='hidden'||e.disabled||e.offsetParent===null)return false;"   // review V-6: really visible, on screen, not covered
           L"if(e.checkVisibility&&!e.checkVisibility({opacityProperty:true,visibilityProperty:true,contentVisibilityAuto:true}))return false;"
           L"var r=e.getBoundingClientRect();if(r.width<2||r.height<2||r.right<=0||r.bottom<=0||r.left>=innerWidth||r.top>=innerHeight)return false;"
           L"var h=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);return !!h&&(h===e||e.contains(h)||h.control===e);}"
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

// ---- Capture coverage (owner decision A) --------------------------------------------------------------------
// The capture runs in an ISOLATED world (CaptureWorld) of EVERY document of the tab, its frames included (the
// out-of-process ones are attached before they run, AutoAttachParams), installed through the DevTools protocol
// (Page.addScriptToEvaluateOnNewDocument with a worldName). It reports through a DevTools binding that exists ONLY
// in that world (Runtime.addBinding executionContextName): the page cannot call it, and the origin it reports is
// read in that world (`self.origin`, the document's real origin), so an opaque (sandboxed) document reads "null"
// and never reports (review R-6). The page's own message channel (chrome.webview.postMessage) is never a capture.
// What it reports (once, directly; nothing is kept in web storage):
//   - a classic form submit;
//   - a script-driven login: a TRUSTED click on a button-like element or Enter in a field, confirmed within 10 s by
//     every password field of the login disappearing, or by the page leaving (pagehide / beforeunload) within 2 s
//     of the click / Enter or of the end of the last request the page made after it (fetch / XHR); a failed login
//     (the fields stay, the person goes elsewhere later) reports nothing;
//   - a first step with a username and no password field at all (two-step logins: the host joins it to a later
//     password of the same origin, TwoStep);
//   - change-password forms: the NEW password (autocomplete new-password, else current + new + confirm), the
//     current one only to find the account; a confirmation that differs reports nothing;
//   - whether the password field was filled by the ENGINE (:-webkit-autofill): a migration from its old store.
inline constexpr const wchar_t* CaptureWorld = L"browser-vault-capture";
inline constexpr const wchar_t* CaptureBindingName = L"browserVaultCapture";
inline std::wstring CaptureScript() {
    // Review V-5: the host does not keep the page's DevTools Runtime domain enabled (it would serialise every console call
    // of the page); it adds the binding to each new document's world once that document exists (VaultRebind). A message
    // sent before that is retried for 3 s; one sent while the page leaves goes at once (the binding is there long before).
    return std::wstring(L"(function(){function B(m){var n=0;(function go(){var f=window.") + CaptureBindingName + L";if(typeof f==='function'){f(m);return;}if(++n<15)setTimeout(go,200);})();}"
        L"var O=self.origin;if(O==='null'||location.protocol!=='https:'||!window.isSecureContext||location.origin!==O)return;"
        L"var TOP=window.top===window?'1':'0';"
        // Review V-4: a real user action in THIS document (trusted pointer or key, in this world), and the values the person
        // typed (trusted input) per field: a page can neither fake the action nor plant the password.
        L"var act=-1e9,typed=new WeakMap();function activated(){return performance.now()-act<=10000;}"
        L"['pointerdown','mousedown','keydown'].forEach(function(n){document.addEventListener(n,function(e){if(e.isTrusted)act=performance.now();},true);});"
        L"document.addEventListener('input',function(e){var t=e.target;if(e.isTrusted&&t&&t.tagName==='INPUT')typed.set(t,t.value);},true);"
        L"function shown(e){return !!e&&e.isConnected&&e.type!=='hidden'&&!e.disabled&&e.getClientRects().length>0;}"
        L"function ac(e){return (e.getAttribute('autocomplete')||'').toLowerCase();}"
        L"function list(r,q){return Array.prototype.slice.call(r.querySelectorAll(q));}"
        L"function visiblePw(r){return list(r,'input[type=password]').filter(shown).length>0;}"
        L"function scope(el){var f=el&&(el.form||(el.closest&&el.closest('form')));return f||document;}"
        L"function userField(r){var c=list(r,'input').filter(function(e){var t=(e.type||'text').toLowerCase();return (t==='text'||t==='email'||t==='tel')&&e.value;});"
        L"var h=c.filter(function(e){return ac(e).indexOf('username')>=0;});if(h.length)return h[0];var v=c.filter(shown);return v.length?v[0]:null;}"
        L"function looksUser(e){return ac(e).indexOf('username')>=0||e.type==='email'||/user|login|mail|ident|account|compte/i.test((e.name||'')+' '+(e.id||''));}"
        L"function snap(r){var pw=list(r,'input[type=password]').filter(shown),f=pw.filter(function(e){return e.value;}),u=userField(r);"
        L"if(!f.length)return (!pw.length&&u&&looksUser(u))?{k:'user',u:u.value}:null;"
        L"var nw=f.filter(function(e){return ac(e).indexOf('new-password')>=0;}),cu=f.filter(function(e){return ac(e).indexOf('current-password')>=0;}),p=null,c='';"
        L"if(nw.length){if(nw.length>1&&nw[0].value!==nw[1].value)return null;p=nw[0];var o=cu.length?cu[0]:f.filter(function(e){return nw.indexOf(e)<0;})[0];c=o?o.value:'';}"
        L"else if(f.length>=3){if(f[1].value!==f[2].value)return null;c=f[0].value;p=f[1];}"
        L"else if(f.length===2){if(f[0].value===f[1].value)p=f[0];else if(cu.length){c=cu[0].value;p=f[0]===cu[0]?f[1]:f[0];}else{c=f[0].value;p=f[1];}}"
        L"else p=f[0];"
        L"if(c===p.value)c='';var a=false;try{a=p.matches(':-webkit-autofill');}catch(x){}"
        L"if(!(a||typed.get(p)===p.value))return null;"   // V-4: typed by the person, or autofilled by the engine
        L"return {k:'capture',u:u?u.value:'',p:p.value,c:c,a:a?'1':'0',f:f};}"
        L"function post(s,v){var m={k:s.k,o:O,t:TOP,v:v,u:s.u||''};if(s.k==='capture'){m.p=s.p;m.c=s.c;m.a=s.a;}try{B(JSON.stringify(m));}catch(x){}}"
        L"var pend=null,timer=0;function stop(){pend=null;if(timer){clearInterval(timer);timer=0;}}"
        L"function confirmed(v){if(!pend)return;var s=pend;stop();post(s,v);}"
        L"function recent(){if(!pend)return false;var last=pend.t0;try{performance.getEntriesByType('resource').forEach(function(e){if((e.initiatorType==='fetch'||e.initiatorType==='xmlhttprequest')&&e.startTime>=pend.t0&&e.responseEnd>last)last=e.responseEnd;});}catch(x){}return performance.now()-last<=2000;}"
        L"function leave(){if(recent())confirmed('leave');else stop();}"
        // Review V-7: a login is held until its outcome is known: every visible password field of THIS document gone (« gone »),
        // or the page left (« leave »: the next document then says whether it asks for a password again, k:'doc').
        L"function hold(s){stop();pend=s;pend.t0=performance.now();var at=Date.now();"
        L"timer=setInterval(function(){if(!pend||Date.now()-at>10000){stop();return;}if(!visiblePw(document))confirmed('gone');},250);}"
        L"function trigger(el){if(!activated())return;var s=snap(scope(el));if(!s)return;if(s.k==='user'){post(s,'step');return;}hold(s);}"
        L"document.addEventListener('submit',function(e){if(!(e.isTrusted&&activated()))return;var f=e.target;if(!f||!f.querySelectorAll)return;var s=snap(f);if(!s)return;if(s.k==='user'){post(s,'step');return;}hold(s);},true);"
        L"document.addEventListener('click',function(e){if(!e.isTrusted)return;var t=e.target,b=t&&t.closest?t.closest('button,input[type=submit],input[type=button],input[type=image],[role=button]'):null;if(b)trigger(b);},true);"
        L"document.addEventListener('keydown',function(e){if(!e.isTrusted||e.key!=='Enter')return;var t=e.target;if(t&&t.tagName==='INPUT')trigger(t);},true);"
        L"window.addEventListener('pagehide',leave,true);window.addEventListener('beforeunload',leave,true);"
        L"document.addEventListener('DOMContentLoaded',function(){setTimeout(function(){try{B(JSON.stringify({k:'doc',o:O,t:TOP,pw:visiblePw(document)?'1':'0'}));}catch(x){}},400);});"
        L"})();";
}
inline std::wstring CaptureScriptParams() { return L"{\"source\":" + JsonString(CaptureScript()) + L",\"worldName\":\"" + CaptureWorld + L"\"}"; }
inline std::wstring CaptureBindingParams() { return std::wstring(L"{\"name\":\"") + CaptureBindingName + L"\",\"executionContextName\":\"" + CaptureWorld + L"\"}"; }
// Frames of another site run in their own renderer: attached (paused) before they run, iframes only, then given
// the same world, binding and auto-attach (nested frames), then resumed.
inline std::wstring AutoAttachParams() { return L"{\"autoAttach\":true,\"waitForDebuggerOnStart\":true,\"flatten\":true,\"filter\":[{\"type\":\"iframe\",\"exclude\":false}]}"; }
// A JSON string value starting at `at`, unescaped (\uXXXX kept as UTF-16 units); false when it is not one.
inline bool JsonStringAt(const std::wstring& j, std::size_t at, std::wstring& out) {
    out.clear(); if (at >= j.size() || j[at] != L'"') return false;
    for (std::size_t i = at + 1; i < j.size(); ++i) {
        const wchar_t c = j[i];
        if (c == L'"') return true;
        if (c != L'\\') { out += c; continue; }
        if (++i >= j.size()) return false;
        const wchar_t e = j[i];
        if (e == L'u') { if (i + 4 >= j.size()) return false; unsigned v = 0; for (int k = 1; k <= 4; ++k) { const wchar_t h = j[i + k]; v <<= 4; if (h >= L'0' && h <= L'9') v |= h - L'0'; else if (h >= L'a' && h <= L'f') v |= h - L'a' + 10; else if (h >= L'A' && h <= L'F') v |= h - L'A' + 10; else return false; } out += static_cast<wchar_t>(v); i += 4; }
        else out += e == L'n' ? L'\n' : e == L't' ? L'\t' : e == L'r' ? L'\r' : e == L'b' ? L'\b' : e == L'f' ? L'\f' : e;
    }
    return false;
}
inline void WipeText(std::wstring& s) { if (!s.empty()) SecureZeroMemory(s.data(), s.size() * sizeof(wchar_t)); s.clear(); }
// Target.attachedToTarget: the child session and its type (an engine id: letters and digits only).
struct AttachedTarget { std::wstring sessionId, type; bool waiting = false; };
inline bool ParseAttachedToTarget(const std::wstring& json, AttachedTarget& out) {
    out = AttachedTarget{}; const auto root = JsonRoot(json);
    if (!JsonPlainString(json, JsonMemberAt(json, root, L"sessionId"), out.sessionId) || out.sessionId.empty() || out.sessionId.size() > 64) return false;
    for (wchar_t c : out.sessionId) if (!AsciiIdChar(c)) return false;
    const auto info = JsonMemberAt(json, root, L"targetInfo");
    if (!JsonPlainString(json, JsonMemberAt(json, info, L"type"), out.type)) return false;
    const auto w = JsonMemberAt(json, root, L"waitingForDebugger"); out.waiting = w != std::wstring::npos && json.compare(w, 4, L"true") == 0;
    return true;
}
// Runtime.bindingCalled of the capture binding. The origin is the isolated world's `self.origin`: it must be a
// canonical https origin (an opaque "null", http, or anything else is refused). Fields of the page (user,
// passwords) are only values; no field decides an origin.
struct CaptureMessage {
    enum class Kind { Capture, User, Doc } kind = Kind::Capture;
    std::wstring origin, user, secret, current, via; bool top = false, autofilled = false, passwordField = false;
    CaptureMessage() = default; CaptureMessage(const CaptureMessage&) = default; CaptureMessage& operator=(const CaptureMessage&) = default;
    ~CaptureMessage() { WipeText(secret); WipeText(current); }
};
// Review V-4: a username shown in B04 and stored is at most 128 characters, without control, line-separator or
// text-direction characters (a page must not write a sentence, or reorder text, inside native UI).
inline bool UsernameAcceptable(const std::wstring& u) {
    if (u.size() > 128) return false;
    for (wchar_t c : u) if (c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0x200E || c == 0x200F || (c >= 0x2028 && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069)) return false;
    return true;
}
inline bool ParseCaptureEvent(const std::wstring& event, CaptureMessage& out) {
    out.kind = CaptureMessage::Kind::Capture; out.origin.clear(); out.user.clear(); WipeText(out.secret); WipeText(out.current); out.via.clear(); out.top = out.autofilled = out.passwordField = false;
    const auto root = JsonRoot(event); std::wstring name, payload;
    if (!JsonStringAt(event, JsonMemberAt(event, root, L"name"), name) || name != CaptureBindingName) return false;
    if (!JsonStringAt(event, JsonMemberAt(event, root, L"payload"), payload)) { WipeText(payload); return false; }
    struct Wiper { std::wstring& s; ~Wiper() { WipeText(s); } } wipePayload{payload};
    const auto p = JsonRoot(payload); std::wstring k, t, a;
    auto str = [&](const wchar_t* key, std::wstring& v) { return JsonStringAt(payload, JsonMemberAt(payload, p, key), v); };
    if (!str(L"k", k) || !str(L"o", out.origin)) return false;
    if (out.origin.empty() || CanonicalOrigin(out.origin) != out.origin) { out.origin.clear(); return false; }   // "null" (opaque), http, non-canonical
    str(L"t", t); str(L"v", out.via); str(L"u", out.user); out.top = t == L"1";
    if (k == L"doc") { std::wstring pw; str(L"pw", pw); out.kind = CaptureMessage::Kind::Doc; out.passwordField = pw == L"1"; out.user.clear(); return pw == L"0" || pw == L"1"; }
    if (!UsernameAcceptable(out.user)) return false;   // review V-4: no sentence or control character inside native UI
    if (k == L"user") { out.kind = CaptureMessage::Kind::User; return !out.user.empty(); }
    if (k != L"capture") return false;
    str(L"p", out.secret); str(L"c", out.current); str(L"a", a); out.autofilled = a == L"1";
    if (out.secret.empty() || out.secret.size() > 1024 || out.current.size() > 1024) { WipeText(out.secret); WipeText(out.current); return false; }
    return true;
}
// What B04 offers for a capture, from what the vault holds for that origin (secrets compared, then wiped).
// Never for a password the vault already holds unchanged; an account known by its username: update; no username:
// the account whose CURRENT password was typed (change form), else the only account, else the user chooses (D1);
// unknown: save, worded as a migration when the ENGINE autofilled it (owner decision B).
struct Offer { enum class Kind { None, Save, Update, Choose } kind = Kind::None; std::wstring user; std::vector<std::wstring> accounts; bool migration = false; };
inline Offer DecideOffer(const Store& s, const std::wstring& origin, const std::wstring& user, const std::wstring& secret, const std::wstring& current, bool autofilled) {
    Offer o; if (origin.empty() || CanonicalOrigin(origin) != origin || secret.empty()) return o;
    const auto accounts = s.For(origin);
    auto holds = [&](const Entry& e, const std::wstring& v) { std::wstring got; const bool same = !v.empty() && s.Reveal(e.id, origin, got) && got == v; WipeText(got); return same; };
    if (!user.empty()) {
        for (const auto& e : accounts) if (e.user == user) { if (holds(e, secret)) return o; o.kind = Offer::Kind::Update; o.user = user; return o; }
        o.kind = Offer::Kind::Save; o.user = user; o.migration = autofilled; return o;
    }
    for (const auto& e : accounts) if (holds(e, secret)) return o;
    if (!current.empty()) { const Entry* match = nullptr; int n = 0; for (const auto& e : accounts) if (holds(e, current)) { match = &e; ++n; }
        if (n == 1) { o.kind = Offer::Kind::Update; o.user = match->user; return o; } }
    if (accounts.size() == 1) { o.kind = Offer::Kind::Update; o.user = accounts[0].user; return o; }
    if (accounts.size() > 1) { o.kind = Offer::Kind::Choose; for (const auto& e : accounts) o.accounts.push_back(e.user); return o; }
    o.kind = Offer::Kind::Save; o.migration = autofilled; return o;
}
// Review V-4: a newer capture of a tab replaces its pending offer only when the newer one is not a frame's while the
// pending one is the page's own, and at most once every 5 s (a frame or a page cannot keep swapping the offer away).
inline constexpr std::int64_t ReplaceIntervalMs = 5000;
inline bool MayReplace(bool pendingTop, bool newTop, std::int64_t now, std::int64_t lastReplaceAt) {
    if (pendingTop && !newTop) return false;
    return !(lastReplaceAt && now - lastReplaceAt < ReplaceIntervalMs);
}
// Review V-7: a login that left its page is offered only once the next document of the same origin and kind (page or
// frame) has loaded without a visible password field; asking for a password again means the login failed.
enum class Outcome { Wait, Offer, Drop };
inline Outcome LoginOutcome(const std::wstring& captureOrigin, bool captureTop, const std::wstring& docOrigin, bool docTop, bool docAsksPassword) {
    if (captureOrigin != docOrigin || captureTop != docTop) return Outcome::Wait;
    return docAsksPassword ? Outcome::Drop : Outcome::Offer;
}
// Two-step logins: the username of a first step, joined to a password of the same origin within 3 minutes.
class TwoStep {
public:
    static constexpr std::int64_t WindowMs = 3LL * 60 * 1000;
    void Remember(const std::wstring& origin, const std::wstring& user, std::int64_t now) { if (origin.empty() || user.empty()) return; origin_ = origin; user_ = user; at_ = now; }
    std::wstring For(const std::wstring& origin, std::int64_t now) const { return !user_.empty() && origin == origin_ && now >= at_ && now - at_ <= WindowMs ? user_ : std::wstring(); }
private:
    std::wstring origin_, user_; std::int64_t at_ = 0;
};
// ---- Engine passwords (owner decision B): migrated as the user logs in (B04 « Garder »), never decrypted from the
// engine's store; the activation date is recorded per profile. Review V-3: the grace period counts DAYS OF USE (a
// calendar day, local, on which the active vault opened a tab), a day is counted only once and only when it is later
// than the last one counted (a clock set back counts nothing, a clock set ahead counts one day), and nothing is ever
// deleted while « Mots de passe » is off or the vault is unreadable (nothing could have been kept).
// What happens when it is due is ONE switch (owner decision pending, review V-2): Automatic = the engine's saved
// passwords are cleared (ClearBrowsingData PASSWORD_AUTOSAVE) then a one-time notice; ManualOffer = nothing is deleted,
// the host only reports that a manual « delete the old passwords » offer is due (EngineStep::OfferManual).
//   browser-vault-migration 1 / activated=<unix ms> / cleared=<unix ms, 0 = not yet> / notice=<0|1> / days=<n> / lastday=<yyyymmdd>
enum class EngineCleanupMode { Automatic, ManualOffer };
inline constexpr EngineCleanupMode EngineCleanup = EngineCleanupMode::Automatic;
struct EngineMigration {
    static constexpr int GraceDays = 30;
    static constexpr const char* Header = "browser-vault-migration 1";
    std::int64_t activated = 0, cleared = 0; bool noticeSeen = false; int useDays = 0, lastUseDay = 0;
    // `dayKey`: yyyymmdd of the local calendar day. True when it counted as a new day of use.
    bool RecordUse(int dayKey) { if (dayKey <= lastUseDay) return false; lastUseDay = dayKey; ++useDays; return true; }
    bool CleanupDue(bool passwordsOn, bool vaultReadable) const { return activated > 0 && cleared == 0 && useDays >= GraceDays && passwordsOn && vaultReadable; }
    bool NoticePending() const { return cleared > 0 && !noticeSeen; }
    std::string Write() const { std::ostringstream o; o << Header << "\nactivated=" << activated << "\ncleared=" << cleared << "\nnotice=" << (noticeSeen ? 1 : 0) << "\ndays=" << useDays << "\nlastday=" << lastUseDay << "\n"; return o.str(); }
    static bool Read(const std::string& text, EngineMigration& out) {
        out = EngineMigration{}; std::istringstream in(text); std::string line; bool any = false, act = false;
        if (!std::getline(in, line) || line != Header) return false;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            const auto eq = line.find('='); if (eq == std::string::npos) return false;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1); if (!Store::Digits(v)) return false;
            std::int64_t n = 0; try { n = std::stoll(v); } catch (...) { return false; }
            if (k == "activated") { out.activated = n; act = true; } else if (k == "cleared") out.cleared = n; else if (k == "notice") { if (n > 1) return false; out.noticeSeen = n == 1; }
            else if (k == "days") { if (n > 100000) return false; out.useDays = static_cast<int>(n); } else if (k == "lastday") { if (n > 99991231) return false; out.lastUseDay = static_cast<int>(n); } else return false;
            any = true;
        }
        return any && act && out.activated > 0;
    }
};

} // namespace Vault
