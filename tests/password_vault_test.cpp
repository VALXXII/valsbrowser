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
    Check(s.Save(L"http://exemple.test", L"x", L"y", 4) == Store::Saved::Refused, "an http origin is refused");
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
    const std::wstring script = FillScript(L"a\"b", L"x</script><img src=x onerror=alert(1)>\\\n\x2028");
    Check(script.find(L"</script>") == std::wstring::npos && script.find(L"\\u003c/script\\u003e") != std::wstring::npos && script.find(L"\\u2028") != std::wstring::npos && script.find(L"a\\\"b") != std::wstring::npos, "fill script: values are JSON-escaped (quotes, markup, line separators)");
    Check(script.rfind(L"void 0;") == script.size() - 7, "fill script returns nothing (no secret comes back through the bridge)");
    // Profile transaction.
    const std::wstring dir = std::filesystem::absolute(L"build/password-vault/profile").wstring(); std::error_code ec; std::filesystem::remove_all(dir, ec); std::filesystem::create_directories(dir, ec);
    Store base, local = s;
    const auto res = ProfileStore::Commit<Store>(dir, dir + L"\\passwords", base, local, [](const Store& b, const Store& mm, const Store& t) { return Store::Merge(b, mm, t); });
    std::string bytes; ProfileStore::ReadAll(dir + L"\\passwords", bytes);
    Check(res == ProfileStore::Result::Written && bytes.find("Nouveau") == std::string::npos && local == s, "vault written through the profile transaction, ciphertext only");
    std::printf(failures ? "FAIL password vault: %d\n" : "PASS password vault: DPAPI per origin, file, merge, guards, escaping\n", failures);
    return failures ? 1 : 0;
}
