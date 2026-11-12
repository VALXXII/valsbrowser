// Password vault core (src/password-vault.h): DPAPI bound to the origin, no plaintext on disk,
// interprocess merge, canonical origins, fill guards (frame, document, origin, user action) and
// the escaping of the fill script. Fictitious accounts only; the vault stays inactive.
#include "../src/password-vault.h"
#include "../src/profile-store.h"
#include <cstdio>
#include <filesystem>
#include <sstream>

static int failures = 0;
static void Check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

int main() {
    using namespace Vault;
    Check(!Active(), "the vault is inactive until the independent security review");
    // Canonical origins.
    Check(CanonicalOrigin(L"https://Compte.Exemple.test:443/login?x#y") == L"https://compte.exemple.test", "origin: lower-case host, default port elided, path dropped");
    Check(CanonicalOrigin(L"https://exemple.test:8443/") == L"https://exemple.test:8443", "origin: other port kept");
    Check(CanonicalOrigin(L"http://exemple.test/") .empty() && CanonicalOrigin(L"https://user:pw@exemple.test/").empty() && CanonicalOrigin(L"data:text/html,x").empty() && CanonicalOrigin(L"file:///c:/x").empty(), "origin: http, userinfo, data and file refused");
    // DPAPI.
    const std::wstring origin = L"https://compte.exemple.test", secret = L"Fictif-Mot-de-passe-2026!é";
    const std::string blob = Protect(origin, secret);
    std::wstring back; Check(!blob.empty() && Unprotect(origin, blob, back) && back == secret, "DPAPI round trip for the user");
    Check(!Unprotect(L"https://autre.exemple.test", blob, back), "a blob does not decrypt for another origin (entropy bound to the origin)");
    Check(blob.find("Fictif") == std::string::npos, "the blob holds no plaintext");
    // Store, file and merge.
    Store s; Check(s.Save(origin, L"camille@exemple.test", secret, 1) == Store::Saved::Added, "save: added");
    Check(s.Save(origin, L"camille@exemple.test", secret, 2) == Store::Saved::Unchanged, "save again, same secret: unchanged");
    Check(s.Save(origin, L"camille@exemple.test", L"Nouveau-fictif", 3) == Store::Saved::Updated, "same account, new secret: updated");
    Check(s.Save(L"http://exemple.test", L"x", L"y", 4) == Store::Saved::Refused && s.Save(L"", L"x", L"y", 4) == Store::Saved::Refused, "an http or empty origin is refused (review V3)");
    { const std::string body = "browser-passwords 2\n1\t1\t1\t\teA==\tYWFhYQ==\n"; std::istringstream bad(body + "end\t1\t" + Store::Digest(body) + "\n"); bool dmg = false;
      Check(Store::Read(bad, &dmg).Entries().empty() && dmg, "a stored empty origin is dropped as damage (complete v2 file, isolated)"); }
    std::ostringstream out; s.Write(out);
    Check(out.str().find("Fictif") == std::string::npos && out.str().find("Nouveau") == std::string::npos && out.str().find("camille") == std::string::npos, "the file holds no secret and no plain username");
    std::istringstream in(out.str()); bool damaged = true; Store r = Store::Read(in, &damaged);
    Check(!damaged && r == s, "the file reads back identical");
    const auto id = s.Entries()[0].id; std::wstring got;
    Check(r.Reveal(id, origin, got) && got == L"Nouveau-fictif" && !r.Reveal(id, L"https://autre.exemple.test", got), "reveal only for the account's own origin");
    Store mine = s, theirs = s; mine.Remove(id); theirs.Save(L"https://autre.exemple.test", L"x", L"Fictif-2", 5);
    Store m = Store::Merge(s, mine, theirs);
    Check(m.Entries().size() == 1 && m.Entries()[0].origin == L"https://autre.exemple.test", "merge: removal here applied, the other window's addition kept");
    Store gone = s; gone.Remove(id); Check(Store::Merge(s, s, gone).Entries().empty(), "merge: a removal in another window is never undone");
    { Store edited = s; edited.Save(origin, L"camille@exemple.test", L"Plus-recent-fictif", 9); Check(Store::Merge(s, edited, gone).Entries().size() == 1, "merge: a password changed here outlives its deletion elsewhere (review V6)"); }
    { Store a2 = Store{}, b2 = Store{}; a2.Save(origin, L"u", L"un-fictif", 10); b2.Save(origin, L"u", L"deux-fictif", 11); const Store mm = Store::Merge(Store{}, a2, b2); std::wstring v; Check(mm.Entries().size() == 1 && mm.Reveal(mm.Entries()[0].id, origin, v) && v == L"deux-fictif", "merge: the same account saved in two windows keeps the newest (review V6)"); }
    // Fill guards.
    Target act{L"https://compte.exemple.test/login", L"https://compte.exemple.test/login", 7, 42, true};
    Check(CanFill(act, act, origin) == Refusal::None, "fill: same frame, same document, same origin after a user action");
    Target noAct = act; noAct.userAction = false; Check(CanFill(noAct, act, origin) == Refusal::NoUserAction, "fill: never without a user action (a page message is not one)");
    Target nav = act; nav.navigation = 43; Check(CanFill(act, nav, origin) == Refusal::Stale, "fill: refused after a navigation (stale document)");
    Target other = act; other.frameId = 8; Check(CanFill(act, other, origin) == Refusal::Stale, "fill: refused for another frame");
    Target third = act; third.frameSource = L"https://pub.exemple.test/"; Check(CanFill(third, third, origin) == Refusal::ThirdPartyFrame, "fill: refused in a third-party frame");
    Target http = act; http.frameSource = http.topSource = L"http://compte.exemple.test/"; Check(CanFill(http, http, origin) == Refusal::NotHttps, "fill: refused over http");
    Check(CanFill(act, act, L"https://autre.exemple.test") == Refusal::OtherOrigin, "fill: an account of another origin is refused");
    // Script escaping.
    const std::wstring script = FillScript(origin, L"a\"b", L"x</script><img src=x onerror=alert(1)>\\\n\x2028");
    Check(script.find(L"location.origin!==o") != std::wstring::npos && script.find(L"ancestorOrigins") != std::wstring::npos && script.find(L"isSecureContext") != std::wstring::npos && script.find(L"self.origin!==o") != std::wstring::npos, "fill script re-checks origin, opaque origin, secure context and ancestors when it runs (review V1/V2)");
    Check(script.find(L"a.form?a.form:document") == std::wstring::npos && script.find(L"offsetParent") != std::wstring::npos, "fill script: no whole-document fallback, no hidden field (review V4)");
    Check(script.find(L"</script>") == std::wstring::npos && script.find(L"\\u003c/script\\u003e") != std::wstring::npos && script.find(L"\\u2028") != std::wstring::npos && script.find(L"a\\\"b") != std::wstring::npos, "fill script: values are JSON-escaped (quotes, markup, line separators)");
    Check(script.find(L"return p") == std::wstring::npos && script.find(L"return u") == std::wstring::npos && script.find(L"return 'filled';") != std::wstring::npos, "fill script answers only a fixed status word (no secret comes back through the bridge)");
    Check(ParseFillResult(0, L"\"filled\"") == FillOutcome::Filled && ParseFillResult(0, L"\"noform\"") == FillOutcome::NoForm && ParseFillResult(0, L"\"origin\"") == FillOutcome::Origin && ParseFillResult(0, L"\"nofield\"") == FillOutcome::NoField, "fill result: the fixed words");
    Check(ParseFillResult(0, L"null") == FillOutcome::Failed && ParseFillResult(0, L"\"x\"") == FillOutcome::Failed && ParseFillResult(-1, L"\"filled\"") == FillOutcome::Failed && ParseFillResult(0, nullptr) == FillOutcome::Failed, "fill result: anything else, or an engine error, is a failure, never « rempli »");
    // Profile transaction.
    const std::wstring dir = std::filesystem::absolute(L"build/password-vault/profile").wstring(); std::error_code ec; std::filesystem::remove_all(dir, ec); std::filesystem::create_directories(dir, ec);
    Store base, local = s;
    const auto res = ProfileStore::Commit<Store>(dir, dir + L"\\passwords", base, local, [](const Store& b, const Store& mm, const Store& t) { return Store::Merge(b, mm, t); });
    std::string bytes; ProfileStore::ReadAll(dir + L"\\passwords", bytes);
    Check(res == ProfileStore::Result::Written && bytes.find("Nouveau") == std::string::npos && local == s, "vault written through the profile transaction, ciphertext only");
    // ---- Decision S3: fail closed on unknown formats, exact backup before writing a damaged file ----
    {
        const std::vector<std::string> known{"browser-passwords 2"};
        const std::wstring f = dir + L"\\s3"; auto merge = [](const Store& b, const Store& mm, const Store& t) { return Store::Merge(b, mm, t); };
        auto put = [&](const std::string& bytes) { ProfileStore::WriteAtomic(f, bytes); };
        auto bytesOf = [&](const std::wstring& path) { std::string b; ProfileStore::ReadAll(path, b); return b; };
        std::ostringstream one; s.Write(one);
        // Future version of the same family: refused, file and memory untouched.
        const std::string future = "browser-passwords 3\nnew-format-line\n"; put(future);
        { Store b0 = s, m0 = s; Store b1 = b0, m1 = m0; m1.Save(L"https://autre.exemple.test", L"z", L"Fictif-z", 20);
          const Store mBefore = m1; const auto res = ProfileStore::Commit<Store>(dir, f, b1, m1, merge, 5000, known);
          Check(res == ProfileStore::Result::UnknownFormat && bytesOf(f) == future && b1 == b0 && m1 == mBefore, "S3: a future vault format is never overwritten; base and mine untouched"); }
        // The v1 format of this (never activated) lot: refused, not migrated.
        const std::string v1 = "browser-passwords 1\nabc\t1\t1\tYQ==\tYg==\tYw==\n"; put(v1);
        { Store b1, m1 = s; const auto res = ProfileStore::Commit<Store>(dir, f, b1, m1, merge, 5000, known);
          Check(res == ProfileStore::Result::UnknownFormat && bytesOf(f) == v1, "S3: the v1 file of this inactive lot is refused, never migrated or overwritten"); }
        // Not our header at all: refused the same way.
        const std::string garbage("\x00\x01garbage\n", 11); put(garbage);
        { Store b1, m1 = s; const auto res = ProfileStore::Commit<Store>(dir, f, b1, m1, merge, 5000, known);
          Check(res == ProfileStore::Result::UnknownFormat && bytesOf(f) == garbage, "S3: an unreadable header is not taken as an empty vault"); }
        // Known format, one damaged line: exact backup first, readable entries kept, nothing taken as deleted.
        const std::string damaged = one.str() + "not\ta\tvalid\tline\n"; put(damaged);
        ProfileStore::DamagedStampForTest() = L"s3test";
        { Store b1 = s, m1 = s; m1.Save(origin, L"nouveau@exemple.test", L"Fictif-n", 30); const Store mBefore = m1, bBefore = b1;
          const auto res = ProfileStore::Commit<Store>(dir, f, b1, m1, merge, 5000, known, false);
          Check(res == ProfileStore::Result::Failed && bytesOf(f + L".damaged-s3test") == damaged, "S3: a damaged vault is backed up byte for byte");
          Check(bytesOf(f) == damaged && m1 == mBefore && b1 == bBefore, "S3: a damaged vault is never rewritten (fail closed): file, base and mine untouched"); }
        // The exact backup cannot be made: nothing is written.
        put(damaged); std::filesystem::remove(f + L".damaged-s3test", ec); std::filesystem::create_directories(f + L".damaged-s3test", ec);
        { Store b1 = s, m1 = s; m1.Save(L"https://autre.exemple.test", L"y", L"Fictif-y", 40); const Store mBefore = m1, bBefore = b1;
          const auto res = ProfileStore::Commit<Store>(dir, f, b1, m1, merge, 5000, known, false);
          Check(res == ProfileStore::Result::Failed && bytesOf(f) == damaged && m1 == mBefore && b1 == bBefore, "S3: no exact backup, no write (file, base and mine unchanged)"); }
        std::filesystem::remove_all(f + L".damaged-s3test", ec); ProfileStore::DamagedStampForTest().clear();
        // Absent file: a new vault is accepted.
        std::filesystem::remove(f, ec);
        { Store b1, m1 = s; Check(ProfileStore::Commit<Store>(dir, f, b1, m1, merge, 5000, known) == ProfileStore::Result::Written && !bytesOf(f).empty(), "S3: an absent file is a new vault (written)"); }
        // Busy: another thread holds the profile lock; nothing changes.
        { HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr), done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
          struct Ctx { std::wstring dir; HANDLE ready, done; } ctx{dir, ready, done};
          HANDLE th = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD { auto* c = static_cast<Ctx*>(p); ProfileStore::Lock l(c->dir); SetEvent(c->ready); WaitForSingleObject(c->done, 10000); return 0; }, &ctx, 0, nullptr);
          WaitForSingleObject(ready, 5000);
          Store b1 = s, m1 = s; m1.Remove(m1.Entries()[0].id); const Store mBefore = m1, bBefore = b1; const std::string fileBefore = bytesOf(f);
          const auto res = ProfileStore::Commit<Store>(dir, f, b1, m1, merge, 200, known);
          Check(res == ProfileStore::Result::Busy && m1 == mBefore && b1 == bBefore && bytesOf(f) == fileBefore, "S3: lock busy, nothing written, base and mine untouched");
          SetEvent(done); WaitForSingleObject(th, 5000); CloseHandle(th); CloseHandle(ready); CloseHandle(done); }
    }
    // ---- v2 completeness proof: every boundary cut, lost entry, trailer, stray bytes, count ----------
    {
        Store three; three.Save(L"https://a.exemple.test", L"a", L"Fictif-a", 1); three.Save(L"https://b.exemple.test", L"b", L"Fictif-b", 2); three.Save(L"https://c.exemple.test", L"c", L"Fictif-c", 3);
        std::ostringstream o; three.Write(o); const std::string full = o.str();
        auto read = [](const std::string& t, bool& dmg) { std::istringstream in(t); return Store::Read(in, &dmg); };
        { bool d = true; const Store r = read(full, d); Check(!d && r == three, "v2: a complete file reads back identical"); }
        { Store empty; std::ostringstream e; empty.Write(e); bool d = true; const Store r = read(e.str(), d); Check(!d && r.Entries().empty() && e.str().find("end\t0\t") != std::string::npos, "v2: a legitimately empty vault is explicit and not damage"); }
        { bool d = true; read("", d); Check(!d, "v2: an absent (zero-byte) file is a new vault, not damage"); }
        // Cut at EVERY line boundary (after the header, after each record, before and inside the trailer).
        bool allCut = true; int cuts = 0;
        for (std::size_t pos = 0; pos < full.size(); ++pos) if (full[pos] == '\n' && pos + 1 < full.size()) { bool d = false; read(full.substr(0, pos + 1), d); allCut = allCut && d; ++cuts; }
        Check(allCut && cuts >= 4, "v2: a file cut at any record boundary (or before/inside the trailer) is detected");
        { bool d = false; read(full.substr(0, full.size() - 1), d); Check(d, "v2: the trailer's final newline missing is detected"); }
        { const auto a = full.find('\n') + 1, b = full.find('\n', a) + 1; std::string lost = full.substr(0, a) + full.substr(b); bool d = false; read(lost, d); Check(d, "v2: one whole entry removed is detected (count and digest)"); }
        { std::string wrongCount = full; const auto t = wrongCount.rfind("end\t3\t"); wrongCount.replace(t, 6, "end\t2\t"); bool d = false; read(wrongCount, d); Check(d, "v2: a wrong count is detected"); }
        { std::string wrongDigest = full; wrongDigest[wrongDigest.size() - 2] = wrongDigest[wrongDigest.size() - 2] == 'a' ? 'b' : 'a'; bool d = false; read(wrongDigest, d); Check(d, "v2: a wrong digest is detected"); }
        { bool d = false; read(full + "x", d); bool d2 = false; read(full + "end\t0\tabc\n", d2); Check(d && d2, "v2: bytes after the trailer are detected"); }
        { const std::string noTrailer = full.substr(0, full.rfind("end\t")); bool d = false; read(noTrailer, d); Check(d, "v2: a missing trailer is detected"); }
        { std::string over = "browser-passwords 2\n"; const std::string tr = "end\t5001\t" + Store::Digest(over) + "\n"; bool d = false; read(over + tr, d); Check(d, "v2: a count over the limit is detected"); }
        // A voluntary deletion stays a deletion through a valid file.
        Store base3 = three, mine3 = three; mine3.Remove(three.Entries()[1].id);
        std::ostringstream w; Store::Merge(base3, mine3, three).Write(w); bool d = true; const Store back = read(w.str(), d);
        Check(!d && back.Entries().size() == 2 && !back.Find(three.Entries()[1].id), "v2: a voluntary deletion is written as a valid, complete file and not resurrected");
    }
    // ---- Review fixes V2-M1/M2/M3 and isolated checks -------------------------------------------
    {
        auto bytesOf = [&](const std::wstring& path) { std::string b; ProfileStore::ReadAll(path, b); return b; };
        const std::wstring f = dir + L"\\v2fix";
        // V2-M1: an EXISTING zero-byte vault is damage, never an empty vault that « deletes » this window's accounts.
        ProfileStore::WriteAtomic(f, "");
        { Store b1 = s, m1 = s; const Store mBefore = m1; const auto res = Vault::Commit(dir, f, b1, m1);
          Check(res == ProfileStore::Result::Failed && bytesOf(f).empty() && m1 == mBefore && std::filesystem::exists(f), "V2-M1: a zero-byte existing vault is refused (no account lost)"); }
        std::filesystem::remove(f, ec);
        // V2-M3: the browser's only entry point is fail closed by default (v1 refused, damaged never rewritten).
        ProfileStore::WriteAtomic(f, "browser-passwords 1\n");
        { Store b1, m1 = s; Check(Vault::Commit(dir, f, b1, m1) == ProfileStore::Result::UnknownFormat && bytesOf(f) == "browser-passwords 1\n", "V2-M3: Vault::Commit refuses v1 without any option"); }
        std::ostringstream ok1; s.Write(ok1); const std::string dmg = ok1.str() + "x"; ProfileStore::WriteAtomic(f, dmg);
        { Store b1 = s, m1 = s; m1.Save(L"https://n.exemple.test", L"n", L"Fictif-n", 50); Check(Vault::Commit(dir, f, b1, m1) == ProfileStore::Result::Failed && bytesOf(f) == dmg, "V2-M3: Vault::Commit never rewrites a damaged vault"); }
        std::filesystem::remove(f, ec);
        // V2-M2: over the limit nothing is written and nothing is dropped.
        Store big; for (std::size_t k = 0; k < Store::Limit; ++k) big.Save(L"https://l" + std::to_wstring(k) + L".exemple.test", L"u", L"F", 1);
        Check(big.Entries().size() == Store::Limit, "fixture: a vault at the limit");
        { Store b1, m1 = big; Check(Vault::Commit(dir, f, b1, m1) == ProfileStore::Result::Written, "a vault at the limit is written");
          Store other = big; Store mine2 = big; Store theirs; { std::istringstream in(bytesOf(f)); theirs = Store::Read(in); }
          other.Save(L"https://autre-fenetre.exemple.test", L"x", L"F", 2);   // the file grows elsewhere past... (simulated below)
          Store base2 = theirs, m2 = theirs; m2.Entries();
          // Simulate: another window added one entry beyond this window's view: merged size = Limit + 1.
          Store over = theirs; Store extra; extra.Save(L"https://extra.exemple.test", L"e", L"F", 3);
          std::ostringstream w; { Store tmp = theirs; } (void)w; (void)other; (void)mine2; (void)base2; (void)over;
          Store m3 = theirs; Store b3 = theirs;   // mine adds one entry to a full vault: Save refuses at the limit
          Check(m3.Save(L"https://plus.exemple.test", L"p", L"F", 4) == Store::Saved::Refused, "V2-M2: a save past the limit is refused explicitly (nothing silently dropped)");
          Store merged = Store::Merge(Store{}, extra, theirs); Check(merged.Entries().size() == Store::Limit + 1, "V2-M2: Merge never truncates");
          Store bb, mm = merged; const std::string before = bytesOf(f);
          Check(Vault::Commit(dir, f, bb, mm) == ProfileStore::Result::Failed && bytesOf(f) == before && mm.Entries().size() == Store::Limit + 1, "V2-M2: over the limit the commit fails: file untouched, nothing dropped from memory"); }
        std::filesystem::remove(f, ec);
        // Isolated: a count over the limit with a CORRECT digest and matching entries is refused by the limit check itself.
        { std::string body = "browser-passwords 2\n"; std::ostringstream tr; tr << "end\t" << (Store::Limit + 1) << "\t" << Store::Digest(body) << "\n"; bool d = false; std::istringstream in(body + tr.str()); Store::Read(in, &d); Check(d, "v2: the limit check alone rejects a count over the limit"); }
        // Isolated: a bad entry line inside a COMPLETE v2 file (correct count/digest) is damage.
        { const std::string body = "browser-passwords 2\nnot\ta\tvalid\tline\tat\tall\n"; std::istringstream in(body + "end\t1\t" + Store::Digest(body) + "\n"); bool d = false; Store::Read(in, &d); Check(d, "v2: a malformed line inside a complete file is damage (isolated)"); }
        // Strict fields: a leading '+' or a non-canonical base64 is refused.
        { const std::string body = "browser-passwords 2\n1\t+1\t1\taHR0cHM6Ly9hLmV4ZW1wbGUudGVzdA==\teA==\tYWFhYQ==\n"; std::istringstream in(body + "end\t1\t" + Store::Digest(body) + "\n"); bool d = false; Store::Read(in, &d); Check(d, "v2: lenient numbers (leading +) are refused"); }
    }
    { const std::string body = "browser-passwords 2\n1\t9999999999999999999\t1\taHR0cHM6Ly9hLmV4ZW1wbGUudGVzdA==\teA==\tYWFhYQ==\n"; std::istringstream in(body + "end\t1\t" + Store::Digest(body) + "\n"); bool d = false; Store::Read(in, &d); Check(d, "V2-L2: an out-of-range number is damage, never a crash"); }
    std::printf(failures ? "FAIL password vault: %d\n" : "PASS password vault: DPAPI per origin, file, merge, guards, escaping\n", failures);
    return failures ? 1 : 0;
}
