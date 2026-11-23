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
    // Review N13: the capture survives only its own submission.
    { const std::wstring o = L"https://compte.exemple.test";
      CaptureBinding b; b.Start(o, 0);
      Check(b.NavigationStarting(5, o) && b.NavigationStarting(5, o) && b.NavigationCompleted(5, true), "capture: the submission (first navigation after it, same origin) and its same-origin redirect keep the offer");
      Check(!b.NavigationStarting(6, o) && !b.Active(), "capture: a second navigation, even of the same origin, drops the offer");
      CaptureBinding c; c.Start(o, 0); Check(!c.NavigationStarting(5, L"https://banque.exemple.test"), "capture: an independent navigation to another origin, even immediate, drops the offer");
      CaptureBinding d; d.Start(o, 0); d.NavigationStarting(5, o); Check(!d.NavigationStarting(5, L"https://autre.exemple.test"), "capture: a redirect of the submission to another origin drops the offer");
      CaptureBinding e; e.Start(o, 0); e.NavigationStarting(5, o); Check(!e.NavigationCompleted(5, false) && !e.Active(), "capture: a failed submission drops the offer");
      CaptureBinding f; f.Start(o, 9); Check(f.NavigationStarting(9, o) && !f.NavigationStarting(10, o), "capture: message after the submission started: the in-flight navigation is the submission, the next one drops");
      CaptureBinding g; g.Start(o, 0); Check(!g.NavigationStarting(5, L""), "capture: an empty target origin (data:, http) counts as another origin");
      CaptureBinding h; Check(!h.NavigationStarting(1, o), "capture: no binding, nothing kept"); }
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
    // Review R-1: the fill runs in an ISOLATED world of the top-level document (DevTools protocol), never in the
    // page's main world, where self/window.origin are [Replaceable] and can be faked by an opaque-origin page.
    {
        const std::wstring tree = L"{\"frameTree\":{\"frame\":{\"id\":\"8F1E2D3C4B5A69788796A5B4C3D2E1F0\",\"loaderId\":\"AB12\",\"url\":\"https://compte.exemple.test/login\",\"securityOrigin\":\"https://compte.exemple.test\"},\"childFrames\":[{\"frame\":{\"id\":\"CHILD01\"}}]}}";
        Check(MainFrameId(tree) == L"8F1E2D3C4B5A69788796A5B4C3D2E1F0", "R-1: the main frame id comes from the engine's frame tree (the top frame, never a child)");
        const std::wstring sorted = L"{\"frameTree\": {\"childFrames\": [{\"frame\": {\"id\": \"CHILD01\"}}], \"frame\": {\"adFrameStatus\": {\"adFrameType\": \"none\"}, \"crossOriginIsolatedContextType\": \"NotIsolated\", \"id\": \"MAIN02\", \"url\": \"https://compte.exemple.test/\"}}}";
        Check(MainFrameId(sorted) == L"MAIN02", "R-1: any key order and spacing (as the engine re-serialises it): still the top frame's own id, not a child's or a nested object's");
        Check(MainFrameId(L"{\"frameTree\":{\"childFrames\":[{\"frame\":{\"id\":\"CHILD01\"}}]}}").empty(), "R-1: no top frame object: refused (a child frame's id is never taken)");
        Check(MainFrameId(L"{\"frameTree\":{\"frame\":{\"id\":\"A\\\"B\"}}}").empty() && MainFrameId(L"{}").empty() && MainFrameId(L"{\"frameTree\":{\"frame\":{\"id\":\"\"}}}").empty(), "R-1: a missing, empty or unexpected frame id is refused (nothing is injected)");
        const std::wstring world = IsolatedWorldParams(L"8F1E2D3C");
        Check(world.find(L"\"frameId\":\"8F1E2D3C\"") != std::wstring::npos && world.find(L"\"grantUniveralAccess\":false") != std::wstring::npos, "R-1: a fresh isolated world on that frame, without universal access");
        Check(ContextIdOf(L"{\"executionContextId\":17}") == 17 && ContextIdOf(L"{ \"executionContextId\" : 18 }") == 18 && ContextIdOf(L"{\"x\":{\"executionContextId\":19}}") == 0 && ContextIdOf(L"{\"executionContextId\":-3}") == 0 && ContextIdOf(L"{}") == 0 && ContextIdOf(L"{\"executionContextId\":\"17\"}") == 0, "R-1: the world's execution context id is read strictly (0 = refused)");
        const std::wstring ev = IsolatedEvaluateParams(17, FillScript(origin, L"u", L"p\"</script>"));
        Check(ev.find(L"\"contextId\":17") != std::wstring::npos && ev.find(L"\"returnByValue\":true") != std::wstring::npos && ev.find(L"</script>") == std::wstring::npos && ev.find(L"self.origin!==o") != std::wstring::npos, "R-1: the fill script is evaluated in that world only (JSON-escaped, by value)");
        Check(ParseIsolatedFillResult(0, L"{\"result\":{\"type\":\"string\",\"value\":\"filled\"}}") == FillOutcome::Filled && ParseIsolatedFillResult(0, L"{\"result\":{\"type\":\"string\",\"value\":\"origin\"}}") == FillOutcome::Origin, "R-1: the world's answer: the fixed words");
        Check(ParseIsolatedFillResult(0, L"{\"result\":{\"type\":\"object\",\"value\":\"filled\"}}") == FillOutcome::Failed && ParseIsolatedFillResult(0, L"{\"result\":{\"type\":\"string\",\"value\":\"filled\"},\"exceptionDetails\":{}}") == FillOutcome::Failed && ParseIsolatedFillResult(-1, L"{\"result\":{\"type\":\"string\",\"value\":\"filled\"}}") == FillOutcome::Failed && ParseIsolatedFillResult(0, nullptr) == FillOutcome::Failed, "R-1: an exception, another type or an engine error is a failure, never « rempli »");
    }
    // ---- Capture coverage (owner decision A): the capture runs in an ISOLATED world of every frame and reports
    // through a DevTools binding that exists only there (a page can neither call it nor fake its origin). -------
    {
        const std::wstring cs = CaptureScript();
        Check(cs.find(L"self.origin") != std::wstring::npos && cs.find(L"'null'") != std::wstring::npos && cs.find(L"isSecureContext") != std::wstring::npos && cs.find(L"'https:'") != std::wstring::npos,
              "coverage: the capture script refuses opaque (sandboxed) and non-https documents at document creation (R-6)");
        Check(cs.find(CaptureBindingName) != std::wstring::npos && cs.find(L"chrome.webview") == std::wstring::npos && cs.find(L"postMessage") == std::wstring::npos,
              "coverage: the capture reports only through the isolated binding, never through the page's message channel");
        Check(cs.find(L"localStorage") == std::wstring::npos && cs.find(L"sessionStorage") == std::wstring::npos && cs.find(L"indexedDB") == std::wstring::npos && cs.find(L"document.cookie") == std::wstring::npos && cs.find(L"console.") == std::wstring::npos,
              "coverage: the capture script keeps no secret in web storage, cookies or the console");
        Check(cs.find(L"'submit'") != std::wstring::npos && cs.find(L"'click'") != std::wstring::npos && cs.find(L"'keydown'") != std::wstring::npos && cs.find(L"isTrusted") != std::wstring::npos && cs.find(L"'Enter'") != std::wstring::npos,
              "coverage: classic submit, and script-driven logins started by a trusted click or Enter");
        Check(cs.find(L"isConnected") != std::wstring::npos && cs.find(L"'pagehide'") != std::wstring::npos && cs.find(L"10000") != std::wstring::npos,
              "coverage: a script-driven login is confirmed only by the page leaving or the password field disappearing, within 10 s");
        Check(cs.find(L"getEntriesByType('resource')") != std::wstring::npos && cs.find(L"<=2000") != std::wstring::npos && cs.find(L"else stop()") != std::wstring::npos,
              "coverage: leaving the page counts only within 2 s of the login's click or of its last request (a failed login left later offers nothing)");
        Check(cs.find(L"new-password") != std::wstring::npos && cs.find(L"current-password") != std::wstring::npos,
              "coverage: change-password forms are recognised by autocomplete new-password / current-password");
        Check(cs.find(L":-webkit-autofill") != std::wstring::npos, "coverage: the engine's own autofill is detected (migration wording)");
        Check(cs.find(L"window.top===window") != std::wstring::npos, "coverage: frames report themselves as frames (the offer is for the frame's own origin)");
        const std::wstring add = CaptureScriptParams();
        Check(add.find(L"\"worldName\":\"browser-vault-capture\"") != std::wstring::npos && add.find(L"\"source\":") != std::wstring::npos && add.find(L"</script>") == std::wstring::npos,
              "coverage: the script is installed in the named isolated world of every new document (Page.addScriptToEvaluateOnNewDocument)");
        Check(CaptureBindingParams() == std::wstring(L"{\"name\":\"") + CaptureBindingName + L"\",\"executionContextName\":\"browser-vault-capture\"}",
              "coverage: the binding exists only in that isolated world (Runtime.addBinding executionContextName)");
        const std::wstring aa = AutoAttachParams();
        Check(aa.find(L"\"autoAttach\":true") != std::wstring::npos && aa.find(L"\"waitForDebuggerOnStart\":true") != std::wstring::npos && aa.find(L"\"flatten\":true") != std::wstring::npos && aa.find(L"\"type\":\"iframe\"") != std::wstring::npos,
              "coverage: out-of-process (cross-site) frames are attached before they run, iframes only");
        AttachedTarget at;
        Check(ParseAttachedToTarget(L"{\"sessionId\":\"S1A2\",\"targetInfo\":{\"targetId\":\"T9\",\"type\":\"iframe\",\"url\":\"https://autre.exemple.test/\"},\"waitingForDebugger\":true}", at) && at.sessionId == L"S1A2" && at.type == L"iframe" && at.waiting,
              "coverage: an attached iframe target: its session id and type");
        Check(!ParseAttachedToTarget(L"{\"sessionId\":\"S\\\"X\",\"targetInfo\":{\"type\":\"iframe\"}}", at) && !ParseAttachedToTarget(L"{}", at),
              "coverage: a malformed attach event is ignored (never a session id with quotes)");
        // The binding event: origin from the isolated world (self.origin), never from the page.
        auto ev = [](const std::wstring& payload) { return L"{\"name\":\"" + std::wstring(CaptureBindingName) + L"\",\"payload\":" + JsonString(payload) + L",\"executionContextId\":7}"; };
        CaptureMessage cm;
        Check(ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"https://compte.exemple.test\",\"t\":\"1\",\"v\":\"submit\",\"u\":\"camille\",\"p\":\"Fictif-\\\"1\",\"c\":\"\",\"a\":\"0\"}"), cm)
              && cm.kind == CaptureMessage::Kind::Capture && cm.origin == L"https://compte.exemple.test" && cm.top && cm.user == L"camille" && cm.secret == L"Fictif-\"1" && cm.current.empty() && !cm.autofilled,
              "coverage: a capture event is read (origin, top frame, user, secret)");
        Check(ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"https://sso.exemple.test:8443\",\"t\":\"0\",\"v\":\"gone\",\"u\":\"\",\"p\":\"Nouveau\",\"c\":\"Ancien\",\"a\":\"1\"}"), cm) && !cm.top && cm.current == L"Ancien" && cm.autofilled && cm.origin == L"https://sso.exemple.test:8443",
              "coverage: a frame's capture keeps the frame's own origin; current password and engine autofill flag read");
        Check(ParseCaptureEvent(ev(L"{\"k\":\"user\",\"o\":\"https://compte.exemple.test\",\"t\":\"1\",\"v\":\"step\",\"u\":\"camille\"}"), cm) && cm.kind == CaptureMessage::Kind::User && cm.secret.empty(),
              "coverage: a first step (username only) is read without any secret");
        Check(!ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"null\",\"t\":\"1\",\"u\":\"x\",\"p\":\"y\"}"), cm) && !ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"http://compte.exemple.test\",\"t\":\"1\",\"u\":\"x\",\"p\":\"y\"}"), cm)
              && !ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"https://Compte.exemple.test:443\",\"t\":\"1\",\"u\":\"x\",\"p\":\"y\"}"), cm),
              "coverage: an opaque, http or non-canonical origin is refused (never an offer)");
        Check(!ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"https://compte.exemple.test\",\"t\":\"1\",\"u\":\"x\",\"p\":\"\"}"), cm) && !ParseCaptureEvent(L"{\"name\":\"autre\",\"payload\":\"{}\"}", cm) && !ParseCaptureEvent(ev(L"{\"k\":\"autre\",\"o\":\"https://compte.exemple.test\"}"), cm),
              "coverage: an empty password, another binding or an unknown kind is refused");
        // What to offer (B04), from what the vault holds: never for a password it already holds unchanged.
        const std::wstring o2 = L"https://offre.exemple.test"; Store v;
        Check(DecideOffer(v, o2, L"camille", L"Fictif-A", L"", false).kind == Offer::Kind::Save && !DecideOffer(v, o2, L"camille", L"Fictif-A", L"", false).migration, "offer: an unknown account: save");
        Check(DecideOffer(v, o2, L"camille", L"Fictif-A", L"", true).kind == Offer::Kind::Save && DecideOffer(v, o2, L"camille", L"Fictif-A", L"", true).migration, "offer: an unknown account the ENGINE autofilled: save, worded as a migration (« Garder… »)");
        Check(DecideOffer(v, o2, L"", L"Fictif-A", L"", false).kind == Offer::Kind::Save, "offer: no username and nothing held: save (the user types the username)");
        v.Save(o2, L"camille", L"Fictif-A", 1);
        Check(DecideOffer(v, o2, L"camille", L"Fictif-A", L"", false).kind == Offer::Kind::None && DecideOffer(v, o2, L"camille", L"Fictif-A", L"", true).kind == Offer::Kind::None, "offer: never for a password the vault already holds unchanged (typed or autofilled)");
        Check(DecideOffer(v, o2, L"", L"Fictif-A", L"", false).kind == Offer::Kind::None, "offer: no username, but the vault holds this password for the site: nothing");
        { const Offer u = DecideOffer(v, o2, L"camille", L"Fictif-B", L"", true); Check(u.kind == Offer::Kind::Update && u.user == L"camille" && !u.migration, "offer: a known account with a new password: update (not a migration)"); }
        { const Offer u = DecideOffer(v, o2, L"", L"Fictif-B", L"", false); Check(u.kind == Offer::Kind::Update && u.user == L"camille", "offer: no username, one account for the site: update that one, shown to the user"); }
        v.Save(o2, L"dominique", L"Fictif-D", 2);
        { const Offer c = DecideOffer(v, o2, L"", L"Fictif-N", L"", false); Check(c.kind == Offer::Kind::Choose && c.accounts.size() == 2 && c.user.empty(), "offer: no username, several accounts: the user chooses (D1)"); }
        { const Offer ch = DecideOffer(v, o2, L"", L"Fictif-N", L"Fictif-D", false); Check(ch.kind == Offer::Kind::Update && ch.user == L"dominique", "offer: change-password form without a username: the account whose CURRENT password was typed is updated"); }
        Check(DecideOffer(v, o2, L"", L"Fictif-D", L"Fictif-A", false).kind == Offer::Kind::None, "offer: change-password whose new password is already held: nothing");
        Check(DecideOffer(v, L"http://offre.exemple.test", L"x", L"y", L"", false).kind == Offer::Kind::None && DecideOffer(v, o2, L"x", L"", L"", false).kind == Offer::Kind::None, "offer: never on http, never without a password");
        // Two-step logins: the username of step 1 joins the password of a later page of the SAME origin, within 3 min.
        TwoStep ts; ts.Remember(o2, L"camille", 1000);
        Check(ts.For(o2, 1000 + 60000) == L"camille" && ts.For(o2, 1000 + TwoStep::WindowMs) == L"camille", "two-step: the first step's username, within the window");
        Check(ts.For(o2, 1000 + TwoStep::WindowMs + 1).empty() && ts.For(L"https://autre.exemple.test", 2000).empty(), "two-step: never after the window, never for another origin");
        ts.Remember(o2, L"", 3000); Check(ts.For(o2, 3500) == L"camille", "two-step: an empty username never erases the remembered one");
        // Engine passwords, owner decision « option A »: NEVER deleted automatically. After 30 DAYS OF USE (a day on which the
        // vault was active and a tab opened; a clock set back or ahead cannot shorten it) the person is OFFERED a manual
        // deletion, only while « Mots de passe » is on and the vault is readable; once deleted, the offer is gone for good.
        EngineMigration mg; const std::int64_t t0 = 1790000000000LL;
        Check(EngineCleanup == EngineCleanupMode::ManualOffer, "option A: the engine's saved passwords are deleted only on the person's request, never automatically");
        Check(EngineMigration::GraceDays == 30, "migration: the offer waits 30 days of use");
        Check(!mg.OfferDue(true, true), "migration: no activation date recorded: no offer");
        mg.activated = t0;
        int key = 20261101, counted = 0; for (int d = 0; d < 29; ++d) counted += mg.RecordUse(key + d) ? 1 : 0;
        Check(counted == 29 && mg.useDays == 29 && !mg.OfferDue(true, true), "migration: 29 days of use: no offer yet");
        Check(!mg.RecordUse(key + 28) && mg.useDays == 29, "migration: the same day again counts nothing");
        Check(!mg.RecordUse(key) && !mg.RecordUse(20250101) && mg.useDays == 29, "migration: a clock set back counts nothing");
        Check(mg.RecordUse(20300101) && mg.useDays == 30, "migration: a clock set years ahead counts ONE day, not years");
        Check(mg.OfferDue(true, true), "migration: the offer is due on the 30th day of use");
        Check(!mg.OfferDue(false, true), "migration: no offer while « Mots de passe » is off (nothing could have been kept)");
        Check(!mg.OfferDue(true, false), "migration: no offer while the vault is unreadable (nothing could have been kept)");
        { EngineMigration away; away.activated = t0; away.RecordUse(20261101); away.RecordUse(20261215); Check(away.useDays == 2 && !away.OfferDue(true, true), "migration: a person who comes back after weeks away has used the browser 2 days, not 45"); }
        mg.cleared = t0 + 1; Check(!mg.OfferDue(true, true), "migration: once the person deleted them, the offer is gone for good");
        EngineMigration back2; Check(EngineMigration::Read(mg.Write(), back2) && back2.activated == mg.activated && back2.cleared == mg.cleared && back2.useDays == 30 && back2.lastUseDay == 20300101 && mg.Write().find("notice=") == std::string::npos, "migration: the state file reads back identical (no notice flag any more)");
        { EngineMigration old; Check(EngineMigration::Read("browser-vault-migration 1\nactivated=12\ncleared=0\nnotice=1\ndays=3\nlastday=20261101\n", old) && old.useDays == 3 && old.cleared == 0, "migration: a state file of the previous build (with its notice flag) still reads"); }
        EngineMigration bad; Check(!EngineMigration::Read("autre chose\n", bad) && !EngineMigration::Read("browser-vault-migration 1\nactivated=12x\n", bad) && !EngineMigration::Read("", bad) && !EngineMigration::Read("browser-vault-migration 1\nactivated=12\nlastday=2026-11-01\n", bad), "migration: an unknown or damaged state file is refused (never taken as a date)");
        // ---- Review V-4: no offer without a real user action in that document; the page cannot write the username at will -----
        Check(cs.find(L"e.isTrusted&&activated()") != std::wstring::npos && cs.find(L"'pointerdown'") != std::wstring::npos && cs.find(L"function activated(){return performance.now()-act<=10000;}") != std::wstring::npos,
              "V-4: a submit counts only when trusted and after a trusted click / key in that document within 10 s");
        Check(cs.find(L"typed.get(p)===p.value") != std::wstring::npos && cs.find(L"'input'") != std::wstring::npos,
              "V-4: the password offered is one the person typed (trusted input) or the engine autofilled, never a value the page planted");
        { const std::wstring ok128(128, L'a'), long129(129, L'a');
          Check(ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"https://compte.exemple.test\",\"t\":\"1\",\"u\":\"" + ok128 + L"\",\"p\":\"x\"}"), cm) && !ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"https://compte.exemple.test\",\"t\":\"1\",\"u\":\"" + long129 + L"\",\"p\":\"x\"}"), cm),
                "V-4: a username longer than 128 characters is refused (no sentence inside native UI)");
          Check(!ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"https://compte.exemple.test\",\"t\":\"1\",\"u\":\"a\\nb\",\"p\":\"x\"}"), cm) && !ParseCaptureEvent(ev(L"{\"k\":\"capture\",\"o\":\"https://compte.exemple.test\",\"t\":\"1\",\"u\":\"a\\u202eb\",\"p\":\"x\"}"), cm) && !ParseCaptureEvent(ev(L"{\"k\":\"user\",\"o\":\"https://compte.exemple.test\",\"t\":\"1\",\"u\":\"a\\u0007\"}"), cm),
                "V-4: control and text-direction characters in a username are refused"); }
        Check(!MayReplace(true, false, 100000, 0) && MayReplace(true, true, 100000, 0) && MayReplace(false, false, 100000, 0) && MayReplace(false, true, 100000, 0), "V-4: a frame never replaces the page's own pending offer");
        Check(!MayReplace(true, true, 100000, 96000) && MayReplace(true, true, 100000, 95000) && !MayReplace(false, false, 100000, 99000), "V-4: a pending offer is replaced at most once every 5 s");
        // ---- Review V-7: a login is offered only once its outcome is known (no password field asked again) ----------------
        Check(ParseCaptureEvent(ev(L"{\"k\":\"doc\",\"o\":\"https://compte.exemple.test\",\"t\":\"1\",\"pw\":\"0\"}"), cm) && cm.kind == CaptureMessage::Kind::Doc && !cm.passwordField
              && ParseCaptureEvent(ev(L"{\"k\":\"doc\",\"o\":\"https://compte.exemple.test\",\"t\":\"0\",\"pw\":\"1\"}"), cm) && cm.passwordField && !cm.top, "V-7: a new document reports whether it asks for a password");
        Check(cs.find(L"k:'doc'") != std::wstring::npos && cs.find(L"'DOMContentLoaded'") != std::wstring::npos && cs.find(L"visiblePw(document)") != std::wstring::npos, "V-7: each document reports, once loaded, whether a visible password field is there; « gone » means none in the whole document");
        const std::wstring oc = L"https://compte.exemple.test";
        Check(LoginOutcome(oc, true, oc, true, false) == Outcome::Offer && LoginOutcome(oc, true, oc, true, true) == Outcome::Drop, "V-7: the next page of the login without a password field: offer; asking for a password again (login failed): drop");
        Check(LoginOutcome(oc, true, L"https://autre.exemple.test", true, false) == Outcome::Wait && LoginOutcome(oc, true, oc, false, false) == Outcome::Wait && LoginOutcome(oc, false, oc, true, false) == Outcome::Wait && LoginOutcome(oc, false, oc, false, false) == Outcome::Offer,
              "V-7: only a document of the same origin and the same kind (page / frame) decides");
        // ---- Review V-6: the fill writes only into fields the person can see --------------------------------------------
        { const std::wstring fs = FillScript(origin, L"u", L"p");
          Check(fs.find(L"checkVisibility") != std::wstring::npos && fs.find(L"opacityProperty:true") != std::wstring::npos && fs.find(L"elementFromPoint") != std::wstring::npos && fs.find(L"innerWidth") != std::wstring::npos && fs.find(L"r.width<2") != std::wstring::npos,
                "V-6: invisible, transparent, zero-size, off-screen or covered fields are never filled"); }
    }
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
    {   // Review N-1: the « typed by the person » listeners sit on window, capture phase (they run before any page listener).
        const std::wstring cs = CaptureScript();
        Check(cs.find(L"window.addEventListener('input',") != std::wstring::npos && cs.find(L"document.addEventListener('input',") == std::wstring::npos, "N-1: the trusted input listener is on window (capture), not on document");
        Check(cs.find(L"forEach(function(n){window.addEventListener(n,") != std::wstring::npos && cs.find(L"window.addEventListener('click',") != std::wstring::npos && cs.find(L"window.addEventListener('keydown',") != std::wstring::npos && cs.find(L"window.addEventListener('submit',") != std::wstring::npos,
              "N-1: pointer/key, click, Enter and submit listeners are on window too");
        bool allCapture = true; for (std::size_t at = cs.find(L"window.addEventListener("); at != std::wstring::npos; at = cs.find(L"window.addEventListener(", at + 1)) {
            int depth = 0; std::size_t k = at + 23; for (; k < cs.size(); ++k) { if (cs[k] == L'(' || cs[k] == L'{') ++depth; else if (cs[k] == L')' || cs[k] == L'}') { if (--depth == 0 && cs[k] == L')') break; } }
            allCapture = allCapture && k > 5 && cs.compare(k - 5, 5, L",true") == 0; }
        Check(allCapture && cs.find(L"document.addEventListener(") == cs.find(L"document.addEventListener('DOMContentLoaded'"), "N-1: every window listener is registered in the capture phase; document keeps only DOMContentLoaded");
    }
    {   // Review N-8: a file over 16 MB is damage, never parsed; the commit never writes one.
        std::string huge = std::string(Store::Header) + "\n" + std::string(static_cast<std::size_t>(Store::MaxFileBytes), 'x'); bool d = false; std::istringstream in(huge);
        Check(Store::Read(in, &d).Entries().empty() && d, "N-8: a vault file over 16 MB is damage (never parsed)");
    }
    // ---- Audit SEC-19 (2026-11-23) ----------------------------------------------------------------------------
    {   // JSON literal: same bytes as the previous implementation, written into one exactly reserved buffer.
        auto reference = [](const std::wstring& s) {   // the implementation before SEC-19 (+= into a growing string)
            std::wstring o = L"\"";
            for (wchar_t c : s) {
                switch (c) { case L'"': o += L"\\\""; break; case L'\\': o += L"\\\\"; break; case L'<': o += L"\\u003c"; break; case L'>': o += L"\\u003e"; break;
                default: if (c < 0x20 || c == 0x2028 || c == 0x2029) { wchar_t b[8]; swprintf(b, 8, L"\\u%04x", static_cast<unsigned>(c)); o += b; } else o += c; }
            }
            return o + L"\"";
        };
        const std::wstring samples[] = {L"", L"F", L"a\"b\\c", L"x</script><img src=x onerror=alert(1)>", std::wstring(L"\x01\x1f\n\t\r", 5), L"\x2028\x2029\x00e9\x20ac", std::wstring(300, L'\x2028') + L"fin"};
        bool same = true, exact = true, stable = true;
        for (const auto& s : samples) {
            const std::wstring j = JsonString(s); same = same && j == reference(s); exact = exact && JsonStringLength(s) == j.size();
            std::wstring out = L"pre"; out.reserve(out.size() + JsonStringLength(s)); const wchar_t* before = out.data(); const auto cap = out.capacity();
            AppendJsonString(out, s); stable = stable && out.data() == before && out.capacity() == cap && out == L"pre" + reference(s);
        }
        Check(same, "SEC-19: JsonString gives exactly the previous escaping (quotes, backslash, <, >, controls, U+2028/2029)");
        Check(exact, "SEC-19: JsonStringLength is the literal's exact length");
        Check(stable, "SEC-19 / V7: appending a literal to a buffer reserved with JsonStringLength never reallocates (no freed copy of the secret)");
        const std::wstring o = L"https://compte.exemple.test", u = L"a\"b", secret = L"F\\ictif</script>\x2028";
        const std::wstring fs = FillScript(o, u, secret);
        Check(fs == std::wstring(FillScriptBody) + reference(o) + L"," + reference(u) + L"," + reference(secret) + L");", "SEC-19: FillScript keeps the same script and arguments, built in place");
        Check(fs.rfind(L"(function(o,u,p){'use strict';", 0) == 0, "SEC-19: the fill function runs in strict mode ('use strict' is its first statement)");
        Check(fs.capacity() == fs.size(), "SEC-19 / V7: FillScript is ONE allocation of its exact size (no reallocated or temporary copy of the secret)");
        const std::wstring ev = IsolatedEvaluateParams(17, fs);
        Check(ev == L"{\"expression\":" + reference(fs) + L",\"contextId\":17,\"returnByValue\":true,\"awaitPromise\":false,\"silent\":true,\"includeCommandLineAPI\":false}", "SEC-19: IsolatedEvaluateParams keeps the same parameters, built in place");
        Check(ev.capacity() == ev.size(), "SEC-19 / V7: the evaluation parameters (they hold the secret) are ONE allocation of their exact size");
    }
    {   // Save at the limit: an existing account can still be updated; only a NEW account is refused.
        Store full; for (std::size_t k = 0; k < Store::Limit; ++k) full.Save(L"https://s" + std::to_wstring(k) + L".exemple.test", L"u", L"Fictif-1", 1);
        Check(full.Entries().size() == Store::Limit, "SEC-19 fixture: a vault at the limit (fictitious accounts)");
        Check(full.Save(L"https://s7.exemple.test", L"u", L"Fictif-2", 2) == Store::Saved::Updated, "SEC-19: at the limit, a changed password of an existing account is updated (was refused)");
        std::wstring back; const auto acc = full.For(L"https://s7.exemple.test");
        Check(acc.size() == 1 && full.Reveal(acc[0].id, L"https://s7.exemple.test", back) && back == L"Fictif-2" && acc[0].updated == 2, "SEC-19: the update at the limit holds the new secret and time");
        Check(full.Save(L"https://s7.exemple.test", L"u", L"Fictif-2", 3) == Store::Saved::Unchanged, "SEC-19: at the limit, the same password is unchanged (not refused)");
        Check(full.Save(L"https://s7.exemple.test", L"autre", L"Fictif-3", 4) == Store::Saved::Refused && full.Save(L"https://neuf.exemple.test", L"u", L"Fictif-3", 4) == Store::Saved::Refused && full.Entries().size() == Store::Limit, "SEC-19: at the limit, a NEW account is still refused");
        // Read: a full file (5000 lines) reads back whole, a duplicate id anywhere is still damage (hash set, not a scan per line).
        std::ostringstream out; full.Write(out); bool d = true; { std::istringstream in(out.str()); Check(Store::Read(in, &d).Entries().size() == Store::Limit && !d, "SEC-19: a vault at the limit reads back whole, undamaged"); }
        std::string text = out.str(); const auto l1 = text.find('\n') + 1, l2 = text.find('\n', l1) + 1, l3 = text.find('\n', l2);
        std::string dup = text.substr(0, l2) + text.substr(l1, l2 - l1).substr(0, text.substr(l1, l2 - l1).find('\t')) + text.substr(text.find('\t', l2), l3 - text.find('\t', l2)) + "\n";
        const auto rest = text.substr(l3 + 1); dup += rest.substr(0, rest.rfind("\nend\t") + 1);
        std::ostringstream tr; tr << "end\t" << Store::Limit << "\t" << Store::Digest(dup) << "\n";
        { std::istringstream in(dup + tr.str()); bool dd = false; const Store r = Store::Read(in, &dd); Check(dd && r.Entries().size() == Store::Limit - 1, "SEC-19: a duplicate id in a complete file is damage (dropped, said)"); }
    }
    {   // VaultCached: the file is re-read only when it changed (stamp: exists, size, write and creation times).
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp); const std::wstring dir = std::wstring(tmp) + L"vault-sec19-" + std::to_wstring(GetCurrentProcessId()); std::error_code ec; std::filesystem::create_directories(dir, ec);
        const std::wstring f = dir + L"\\passwords";
        auto put = [](const std::wstring& path, const std::string& bytes) {   // like the profile store: a new temp file renamed over the vault
            const std::wstring t = path + L".tmp"; HANDLE h = CreateFileW(t.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); DWORD w = 0;
            const bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &w, nullptr); if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            return ok && MoveFileExW(t.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        };
        const FileStamp none = StampOf(f);
        Check(!none.exists && CacheStale(false, L"", FileStamp{}, f, none), "SEC-19 cache: never read: stale");
        Check(put(f, "browser-passwords 2\nAAAA\n"), "SEC-19 cache fixture: synthetic file written");
        const FileStamp s1 = StampOf(f);
        Check(s1.exists && s1.size == 25 && s1 != none && CacheStale(true, f, none, f, s1), "SEC-19 cache: a file that appears is stale");
        Sleep(30); const FileStamp s1b = StampOf(f);
        Check(s1b == s1 && !CacheStale(true, f, s1, f, s1b), "SEC-19 cache: unchanged file: NOT re-read (one attribute query only)");
        Check(CacheStale(true, dir + L"\\autre", s1, f, s1b), "SEC-19 cache: another profile's file (workspace switch): stale");
        Sleep(30); Check(put(f, "browser-passwords 2\nBBBB\n"), "SEC-19 cache fixture: same size, other bytes, replaced like a commit");
        const FileStamp s2 = StampOf(f);
        Check(s2.size == s1.size && s2 != s1 && CacheStale(true, f, s1, f, s2), "SEC-19 cache: a commit by another window (same size) is seen: stale");
        DeleteFileW(f.c_str()); const FileStamp s3 = StampOf(f);
        Check(!s3.exists && CacheStale(true, f, s2, f, s3), "SEC-19 cache: a removed file is seen: stale");
        std::filesystem::remove_all(dir, ec);
    }
    std::printf(failures ? "FAIL password vault: %d\n" : "PASS password vault: DPAPI per origin, file, merge, guards, escaping\n", failures);
    return failures ? 1 : 0;
}
