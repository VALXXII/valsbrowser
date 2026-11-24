// #121: our list under a page field and our validation bubble (design/security-review-autofill-under-field.md, P1–P16).
// Windowless: the typed-info store and its file, the engine import from a fixture « Web Data » (SQLite), the report parser,
// rows, placement, arming, the validation words, the isolated-world script, and the two surfaces rendered OFFSCREEN
// (software Direct2D into PNG, no window) at their Paper sizes. Fictitious values only.
#include "../src/form-memory.h"
#include "../src/field-list.h"
#include "../src/validation-bubble.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static std::wstring Root;   // repository root (assets/fonts)

static std::wstring Event(const std::wstring& payload, long long context = 7, const wchar_t* name = Forms::BindingName) {
    return std::wstring(L"{\"name\":\"") + name + L"\",\"payload\":" + Vault::JsonString(payload) + L",\"executionContextId\":" + std::to_wstring(context) + L"}";
}
static bool MakeWebData(const std::wstring& path, const char* sql) {
    const auto& a = BrowserImport::Sql::Get(); if (!a.Ready()) return false;
    BrowserImport::Sql::sqlite3* db = nullptr;
    if (a.open_v2(BrowserImport::Sql::FileUri(path, "").c_str(), &db, BrowserImport::Sql::OPEN_READWRITE | 0x4 /* CREATE */ | BrowserImport::Sql::OPEN_URI, nullptr) != 0) { if (db) a.close(db); return false; }
    const bool ok = a.exec(db, sql, nullptr, nullptr, nullptr) == 0; a.close(db); return ok;
}
static std::string Bytes(const std::wstring& path) { std::string b; ProfileStore::ReadAll(path, b); return b; }

static void Values() {
    using namespace Forms;
    Check(EmailShaped(L"val@atelier-lumen.fr") && EmailShaped(L"valentin.m@exemple.test"), "e-mail shape: the board's values");
    Check(!EmailShaped(L"valentin.m") && !EmailShaped(L"@x.fr") && !EmailShaped(L"a@b") && !EmailShaped(L"a@@b.fr") && !EmailShaped(L"a b@c.fr") && !EmailShaped(L"a@.fr") && !EmailShaped(L"a@b.fr."), "e-mail shape: refusals (no @, nothing before, no dot, two @, space, dot placement)");
    Check(!EmailShaped(L"a\x202E" L"b@c.fr") && !EmailShaped(L"a\nb@c.fr") && !EmailShaped(std::wstring(250, L'a') + L"@c.fr"), "e-mail shape: no control / text-direction character, at most 254 characters (P12)");
    Check(StartsWithI(L"Val@Atelier.fr", L"va") && !StartsWithI(L"x", L"xy"), "prefix match ignores case");
    Check(OriginOf(L"https://Polar.SH:443/login?x") == L"https://polar.sh" && OriginOf(L"http://127.0.0.1:8080/a") == L"http://127.0.0.1:8080" && OriginOf(L"http://x.test:80/") == L"http://x.test", "OriginOf: as the document reports it (default ports elided)");
    Check(OriginOf(L"https://u@x.test/").empty() && OriginOf(L"file:///c:/x").empty() && OriginOf(L"data:text/html,x").empty(), "OriginOf: userinfo, file and data refused");
}

static void StoreTests(const std::wstring& dir) {
    using namespace Forms;
    Store s; Check(s.Add(L"val@atelier-lumen.fr", 10) && s.Add(L"valentin.m@exemple.test", 20), "add two e-mails");
    Check(!s.Add(L"pas-une-adresse", 30) && s.Entries().size() == 2, "a value that is not an e-mail is never stored (P12)");
    Check(s.Add(L"VAL@atelier-lumen.fr", 40) && s.Entries().size() == 2 && s.Entries()[0].uses == 2, "the same e-mail again (any case): one entry, counted twice");
    auto m = s.Match(L"va", 5); Check(m.size() == 2 && m[0] == L"VAL@atelier-lumen.fr", "match: most recently used first");
    Check(s.Match(L"", 5).empty() && s.Match(L"zz", 5).empty(), "match: an empty or unknown prefix gives nothing");
    Check(s.Touch(L"valentin.m@exemple.test", 50) && s.Match(L"va", 5)[0] == L"valentin.m@exemple.test", "a pick refreshes the date only");
    // File round trip and damage.
    std::ostringstream o; s.Write(o); std::istringstream in(o.str()); bool damaged = true; Store back = Store::Read(in, &damaged);
    Check(!damaged && back == s, "file round trip (header, lines, trailer with digest)");
    Check(o.str().find("atelier") == std::string::npos, "values are stored encoded, never as a readable line (a later reader still needs the digest)");
    std::string cut = o.str(); cut.resize(cut.size() - 10); std::istringstream in2(cut); Store::Read(in2, &damaged); Check(damaged, "a cut file is damage");
    std::string other = o.str(); other[other.find('\t') - 1] ^= 1; std::istringstream in3(other); Store::Read(in3, &damaged); Check(damaged, "a changed byte is damage (digest)");
    std::istringstream in4("browser-form-entries 9\nend\t0\tx\n"); Store::Read(in4, &damaged); Check(damaged, "another version is never read as empty");
    // Limit: the least recently used goes.
    Store full; for (int i = 0; i < 505; ++i) full.Add(L"u" + std::to_wstring(i) + L"@x.fr", i);
    Check(full.Entries().size() == Store::Limit && full.Match(L"u0@", 1).empty() && !full.Match(L"u504@", 1).empty(), "over 500: the least recently used is dropped");
    // Merge.
    Store base; base.Add(L"a@x.fr", 1); base.Add(L"b@x.fr", 1);
    Store mine = base; mine.Remove(L"a@x.fr"); mine.Add(L"c@x.fr", 5);
    Store theirs = base; theirs.Remove(L"b@x.fr"); theirs.Add(L"d@x.fr", 6);
    Store merged = Store::Merge(base, mine, theirs);
    auto has = [&](const Store& st, const wchar_t* v) { return std::any_of(st.Entries().begin(), st.Entries().end(), [&](const Entry& e) { return e.value == v; }); };
    Check(!has(merged, L"a@x.fr") && !has(merged, L"b@x.fr") && has(merged, L"c@x.fr") && has(merged, L"d@x.fr"), "merge: removals on both sides applied, additions on both sides kept");
    Store touched = base; touched.Touch(L"b@x.fr", 9);
    Check(!has(Store::Merge(base, touched, theirs), L"b@x.fr"), "merge: « retirer » in another window wins over a later use here");
    // Commit through the profile store (fail closed), wipe, private windows.
    std::filesystem::create_directories(dir);
    const std::wstring path = dir + L"\\form-entries"; DeleteFileW(path.c_str());
    Store b0, m0; m0.Add(L"val@atelier-lumen.fr", 1);
    Check(Commit(dir, path, b0, m0) == ProfileStore::Result::Written && Bytes(path).rfind(Store::Header, 0) == 0, "commit writes the file");
    { std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc); f << "browser-form-entries 1\ngarbage\n"; }
    Store b1, m1; m1.Add(L"x@y.fr", 2); const std::string before = Bytes(path);
    Check(Commit(dir, path, b1, m1) == ProfileStore::Result::Failed && Bytes(path) == before, "a damaged file is never rewritten");
    { std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc); f << "browser-form-entries 7\n"; }
    Check(Commit(dir, path, b1, m1) == ProfileStore::Result::UnknownFormat, "a future version is never rewritten");
    DeleteFileW(path.c_str()); Store b2, m2; m2.Add(L"x@y.fr", 2); Commit(dir, path, b2, m2);
    Check(Wipe(dir, path, b2, m2) && m2.Entries().empty() && Bytes(path).find("end\t0\t") != std::string::npos, "wipe: an empty store on disk (clear on exit, full clear)");
    WritesForbidden() = true; Store b3, m3; m3.Add(L"p@q.fr", 1);
    Check(Commit(dir, path, b3, m3) == ProfileStore::Result::Failed && !Wipe(dir, path, b3, m3), "a private window never writes (P11)");
    WritesForbidden() = false;
}

static void EngineImport(const std::wstring& dir) {
    using namespace Forms;
    const std::wstring webData = dir + L"\\Web Data"; DeleteFileW(webData.c_str());
    std::vector<EngineEmail> got;
    Check(ReadEngineEmails(webData, got, dir) == EngineImport::Nothing && got.empty(), "no Web Data: nothing to copy");
    const bool made = MakeWebData(webData,
        "CREATE TABLE autofill (name VARCHAR, value VARCHAR, value_lower VARCHAR, date_created INTEGER DEFAULT 0, date_last_used INTEGER DEFAULT 0, count INTEGER DEFAULT 1, PRIMARY KEY (name, value));"
        "INSERT INTO autofill VALUES ('email','val@atelier-lumen.fr','val@atelier-lumen.fr',1,1700000000,3);"
        "INSERT INTO autofill VALUES ('login','Val@Atelier-Lumen.fr','val@atelier-lumen.fr',1,1600000000,1);"
        "INSERT INTO autofill VALUES ('name','Valentin Martin','valentin martin',1,1700000001,1);"
        "INSERT INTO autofill VALUES ('q','recherche','recherche',1,1700000002,1);"
        "INSERT INTO autofill VALUES ('email',' valentin.m@exemple.test ','valentin.m@exemple.test',1,1700000003,2);"
        "CREATE TABLE logins (origin_url VARCHAR, password_value BLOB);");
    Check(made, "fixture Web Data created (winsqlite3)");
    const std::string sourceBefore = Bytes(webData);
    const auto r = ReadEngineEmails(webData, got, dir);
    Check(r == EngineImport::Copied && got.size() == 2, "only the e-mail values of the engine's autofill table are copied (no name, no search, one per e-mail)");
    Check(got.size() == 2 && got[0].value == L"valentin.m@exemple.test" && got[0].usedMs == 1700000003000LL, "values trimmed, last use kept (most recent first)");
    Check(Bytes(webData) == sourceBefore, "the engine's file is never written (P13)");
    HANDLE held = CreateFileW(webData.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(held != INVALID_HANDLE_VALUE && ReadEngineEmails(webData, got, dir) == EngineImport::Busy, "while the engine holds its file: Busy (retried at the next start)");
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    const std::wstring other = dir + L"\\Other"; DeleteFileW(other.c_str());
    MakeWebData(other, "CREATE TABLE keywords (short_name VARCHAR);");
    Check(ReadEngineEmails(other, got, dir) == EngineImport::Nothing, "a Web Data without the autofill table: nothing");
}

static void Reports() {
    using namespace Forms; Report r; long long ctx = 0;
    const std::wstring type = L"{\"k\":\"type\",\"o\":\"https://polar.sh\",\"s\":\"3\",\"v\":\"va\",\"f\":\"email\",\"ac\":\"\",\"pw\":\"0\",\"x\":\"16.8\",\"y\":\"168.6\",\"w\":\"274.4\",\"h\":\"32\",\"d\":\"1.5\",\"vw\":\"1172\",\"vh\":\"730\"}";
    Check(ParseReport(Event(type, 42), r, ctx) && r.kind == Report::Kind::Type && ctx == 42 && r.seq == 3 && r.value == L"va" && r.field == L"email" && !r.password && r.dpr == 1.5 && r.w > 274, "type report parsed, with the engine's context id");
    Check(!ParseReport(Event(type, 42, L"browserVaultCapture"), r, ctx), "the vault's binding is not ours");
    Check(!ParseReport(Event(type, 0), r, ctx), "no engine context id: refused");
    std::wstring bad = type; bad.replace(bad.find(L"\"d\":\"1.5\""), 9, L"\"d\":\"NaN\""); Check(!ParseReport(Event(bad), r, ctx), "a geometry that is not a bounded number is refused");
    std::wstring field = type; field.replace(field.find(L"\"email\""), 7, L"\"password\""); Check(!ParseReport(Event(field), r, ctx), "a field kind other than email / id is refused (never a password field)");
    Check(ParseReport(Event(L"{\"k\":\"pick\",\"o\":\"https://polar.sh\",\"s\":\"3\",\"i\":\"1\"}"), r, ctx) && r.kind == Report::Kind::Pick && r.index == 1, "pick report");
    Check(!ParseReport(Event(L"{\"k\":\"pick\",\"o\":\"https://polar.sh\",\"s\":\"3\",\"i\":\"-1\"}"), r, ctx) && !ParseReport(Event(L"{\"k\":\"pick\",\"o\":\"https://polar.sh\",\"s\":\"3\",\"i\":\"1.5\"}"), r, ctx), "a pick index out of range or not whole is refused");
    Check(ParseReport(Event(L"{\"k\":\"del\",\"o\":\"https://polar.sh\",\"s\":\"3\",\"i\":\"1\"}"), r, ctx) && r.kind == Report::Kind::Remove && r.index == 1, "Suppr on the chosen row: a remove report (board « états »)");
    Check(ParseReport(Event(L"{\"k\":\"save\",\"o\":\"https://polar.sh\",\"v\":\"a@b.fr\\nc@d.fr\"}"), r, ctx) && r.values.size() == 2, "save report: one value per line");
    Check(ParseReport(Event(L"{\"k\":\"invalid\",\"o\":\"https://polar.sh\",\"m\":\"email-at\",\"a\":\"\",\"b\":\"\",\"x\":\"16\",\"y\":\"168\",\"w\":\"274\",\"h\":\"32\",\"d\":\"1\",\"vw\":\"1172\",\"vh\":\"730\"}"), r, ctx) && r.kind == Report::Kind::Invalid && r.message == L"email-at", "invalid report");
    Check(!ParseReport(Event(L"{\"k\":\"open-list\",\"o\":\"https://polar.sh\"}"), r, ctx), "an unknown kind is refused");
    Check(!ParseReport(Event(std::wstring(9000, L' ')), r, ctx), "an oversized payload is refused");
}

static void RowsTests() {
    using namespace Forms;
    Store infos; infos.Add(L"val@atelier-lumen.fr", 30); infos.Add(L"valentin.m@exemple.test", 20); infos.Add(L"other@x.fr", 10);
    auto rows = BuildRows({}, &infos, L"va", false, true);
    Check(rows.accounts.empty() && rows.infos.size() == 2 && rows.typed == 2, "board « Infos enregistrées »: the two matching e-mails, typed letters counted");
    const std::vector<AccountRow> site{{11, L"val@atelier-lumen.fr"}, {12, L"autre-compte"}};
    rows = BuildRows(site, &infos, L"va", true, true);
    Check(rows.accounts.size() == 1 && rows.accounts[0].id == 11 && rows.infos.size() == 1 && rows.infos[0] == L"valentin.m@exemple.test", "board « Liste réunie »: the account on top, the e-mail that is already an account not repeated");
    Check(BuildRows(site, &infos, L"", true, true).Empty(), "nothing typed: no list (P3)");
    Check(BuildRows(site, &infos, L"va", false, true).accounts.empty() && BuildRows(site, &infos, L"va", true, false).infos.empty(), "« Mots de passe » off: no account rows; « Saisie automatique » off or autocomplete=off: no infos");
    Check(BuildRows({}, &infos, L"val@atelier-lumen.fr", false, true).infos.empty(), "an e-mail already typed in full is not offered again");
}

static void Placement() {
    using namespace Forms; RECT out{};
    const RECT page{0, 81, 1180, 815}, field{453, 250, 727, 282};
    Check(Place(field, page, 320, 157, 8, out) && out.left == 453 && out.top == 290 && out.bottom == 447, "under the field, 8 px, left edges aligned (board: x 452, y 290)");
    Check(Place({1000, 250, 1170, 282}, page, 320, 157, 8, out) && out.right == 1180 && out.left == 860, "moved left to stay inside the page (P2)");
    Check(Place({453, 700, 727, 732}, page, 320, 157, 8, out) && out.bottom == 692, "no room below: above the field");
    Check(!Place({453, 120, 727, 152}, {0, 81, 1180, 300}, 320, 157, 8, out), "no room above nor below: not shown");
    Check(!Place({453, 40, 727, 72}, page, 320, 157, 8, out), "a field outside the page area (over the toolbar): not shown");
    Check(!Place(field, {0, 81, 300, 815}, 320, 157, 8, out), "a page narrower than the list: not shown");
    Report r; r.x = 16.8; r.y = 168.6; r.w = 274.4; r.h = 32; r.dpr = 1.5; const RECT f = FieldScreen(r, {100, 200, 900, 900});
    Check(f.left == 125 && f.top == 453 && f.right == 537 && f.bottom == 501, "CSS px of the document to screen px (devicePixelRatio)");
    Check(!Armed(1000, 1499, true) && Armed(1000, 1500, true) && !Armed(1000, 9000, false) && !Armed(0, 9000, true), "armed only after 500 ms in the foreground window (P7)");
}

static void Words() {
    using namespace Forms;
    Check(InvalidText(L"email-at", L"", L"") == L"Il manque le @ dans l\x2019" L"adresse e-mail.", "the board's text, exactly");
    Check(!InvalidText(L"missing", L"", L"").empty() && !InvalidText(L"email-domain", L"", L"").empty() && !InvalidText(L"pattern", L"", L"").empty() && !InvalidText(L"missing-checkbox", L"", L"").empty(), "every validity state has our words");
    Check(InvalidText(L"too-short", L"8", L"3").find(L"8") != std::wstring::npos && InvalidText(L"too-short", L"8", L"3").find(L"3") != std::wstring::npos, "too short: the limit and the count");
    // Board « Listes sous le champ + bulle · états » (1O85-0), word for word.
    Check(InvalidText(L"missing", L"", L"") == L"Remplissez ce champ." && InvalidText(L"missing-checkbox", L"", L"") == L"Cochez cette case pour continuer."
          && InvalidText(L"missing-choice", L"", L"") == L"Choisissez une option." && InvalidText(L"missing-file", L"", L"") == L"Choisissez un fichier.", "board « états »: empty field, checkbox, choice, file");
    Check(InvalidText(L"email-local", L"", L"") == L"Il manque le nom avant le @." && InvalidText(L"email-domain", L"", L"") == L"Il manque la fin de l\x2019" L"adresse, par exemple gmail.com."
          && InvalidText(L"email", L"", L"") == L"Cette adresse e-mail n\x2019" L"est pas valide.", "board « états »: the e-mail cases");
    Check(InvalidText(L"url", L"", L"") == L"\x00C9" L"crivez une adresse web compl\x00E8" L"te, par exemple https://site.fr." && InvalidText(L"pattern", L"", L"") == L"Ce n\x2019" L"est pas le format attendu par le site."
          && InvalidText(L"bad-number", L"", L"") == L"\x00C9" L"crivez un nombre.", "board « états »: web address, format, number");
    Check(InvalidText(L"too-short", L"8", L"5") == L"Au moins 8 caract\x00E8" L"res (5 pour l\x2019" L"instant)." && InvalidText(L"too-long", L"20", L"24") == L"20 caract\x00E8" L"res au plus (24 pour l\x2019" L"instant)."
          && InvalidText(L"underflow", L"1", L"") == L"La valeur doit \x00EA" L"tre 1 ou plus." && InvalidText(L"overflow", L"10", L"") == L"La valeur doit \x00EA" L"tre 10 ou moins.", "board « états »: too short, too long, minimum, maximum"); 
    Check(InvalidText(L"underflow", L"<b>", L"") == InvalidText(L"unknown-kind", L"", L""), "a limit that is not a number is never shown (generic words instead)");
    Check(InvalidText(L"bad-date", L"", L"") == L"Cette date n\x2019" L"est pas valide." && InvalidText(L"bad-time", L"", L"") == L"Cette heure n\x2019" L"est pas valide."
          && InvalidText(L"missing-value", L"", L"") == L"Choisissez une valeur.", "owner decision: invalid date, invalid time, empty date / time / colour / range");
    Check(CustomText(L"Ligne 1\nLigne 2\x202E") == L"Ligne 1 Ligne 2" && CustomText(std::wstring(300, L'x')).size() == 150, "setCustomValidity: one line, plain characters, at most 150 (P14)");
    Check(InvalidText(L"custom", L"\x202E", L"") == InvalidText(L"bad-input", L"", L""), "an empty custom text falls back to our words");
}

static void ScriptTests(const std::wstring& out) {
    using namespace Forms; const std::wstring s = Script();
    auto has = [&](const wchar_t* t) { return s.find(t) != std::wstring::npos; };
    Check(has(L"if(window.top!==window)return;") && has(L"var O=self.origin;if(O==='null'"), "top document only, opaque origin refused (P4)");
    Check(has(L"if(!e.isTrusted)return;var t=p0(e);if(!t||t.tagName!=='INPUT')return;") && has(L"if(t!==keyEl||performance.now()-lastKey>1000)return;"), "the list opens on a trusted key then a trusted input of the same field (P3)");
    Check(has(L"opac(e)<0.5") && has(L"elementFromPoint") && has(L"checkVisibility") && has(L"vv.scale"), "visible, opaque, not covered, no pinch zoom (P5)");
    Check(has(L"if(s!==seq||!cur)return 'stale';var e=cur;if(e!==deep())return 'stale';var r=rectOf(e);if(!r||!same(r,cr,2))return 'moved';"), "the fill re-checks the same element, focused, visible, not moved (P9)");
    Check(has(L"typed.get(e)!==e.value") && has(L"one-time-code"), "only what the person typed is saved (P12)");
    Check(has(L"if(k==='Delete'&&sel>=0&&!e.repeat){e.preventDefault();e.stopImmediatePropagation();send({k:'del'"), "Suppr removes only the row chosen with the keyboard, while a list is shown");
    Check(has(L"return ['missing-value']") && has(L"'bad-date'") && has(L"'bad-time'"), "the world names the new states (empty date / time / colour / range, invalid date, invalid time)");
    Check(has(L"if(ev.defaultPrevented)return;ev.preventDefault();"), "the engine's bubble is cancelled only when the page did not handle it (P14)");
    Check(s.find(L"chrome.webview") == std::wstring::npos && s.find(L"postMessage") == std::wstring::npos, "the page's message channel is never used: only the world's binding");
    Check(FillCall(3, L"a\"</script>@b.fr").find(L"\\u003c/script\\u003e") != std::wstring::npos, "a filled value is a JSON string literal (escaped)");
    std::ofstream js(std::filesystem::path(out) / "forms-world.js", std::ios::binary); js << Vault::Utf8(s);   // node --check (test-form-memory.ps1)
    Check(ParseFillAnswer(0, L"{\"result\":{\"type\":\"string\",\"value\":\"filled\"}}") == FillAnswer::Filled && ParseFillAnswer(0, L"{\"result\":{\"type\":\"string\",\"value\":\"moved\"}}") == FillAnswer::Moved
          && ParseFillAnswer(0, L"{\"result\":{\"type\":\"object\"},\"exceptionDetails\":{}}") == FillAnswer::Failed && ParseFillAnswer(-1, nullptr) == FillAnswer::Failed, "only « filled » from the world counts as filled");
    Check(!EngineAutofill, "the engine's own list stays off (P13)");
}

static void Surfaces(const std::wstring& out) {
    using namespace NativeChrome;
    FieldList::Content a; a.infosTitle = L"Informations enregistrées"; a.infos = {L"val@atelier-lumen.fr", L"valentin.m@example.com"}; a.typed = 2; a.footer = L"Gérer la saisie automatique";
    Check(std::lround(FieldList::Height(a)) == 157 && a.Count() == 3, "« Infos enregistrées · A »: 320 × 157 (Paper 1LDA-0), 3 selectable rows");
    FieldList::Content b; b.accountsTitle = L"Comptes enregistrés pour github.com"; b.accounts = {L"val@atelier-lumen.fr"}; b.infosTitle = L"Informations enregistrées"; b.infos = {L"valentin.m@example.com"}; b.typed = 2; b.footer = L"Gérer les mots de passe et la saisie";
    Check(std::lround(FieldList::Height(b)) == 205, "« Liste réunie · A »: 320 × 205 (Paper 1LTY-0)");
    const auto items = FieldList::Items(b);
    Check(items.size() == 6 && items[0].top == 7 && items[1].top == 39 && items[2].top == 89 && items[2].height == 34 && items[3].top == 125 && items[4].top == 163 && items[5].top == 166, "merged list: every row where Paper puts it (titles 30 / 34, account 48, info 36, separator, footer 32, gap 2)");
    FieldList::List list; const auto png = [&](const wchar_t* name) { return (out + L"\\" + name); };
    Check(list.RenderForTest(Root, a, 0, 96, png(L"infos-96.png").c_str()) && list.RenderForTest(Root, a, 0, 192, png(L"infos-192.png").c_str()), "the infos list renders offscreen (96 and 192 DPI), first row hovered as on the board");
    Check(list.RenderForTest(Root, b, 0, 96, png(L"merged-96.png").c_str()) && list.RenderForTest(Root, b, 0, 192, png(L"merged-192.png").c_str()), "the merged list renders offscreen, the account hovered as on the board");
    Check(list.RenderStateForTest(Root, a, -1, false, false, 1, 96, png(L"keyboard-96.png").c_str()) && list.RenderStateForTest(Root, a, -1, false, false, 1, 192, png(L"keyboard-192.png").c_str()), "board « états »: the row chosen with ↓ (cap « Entrée ») renders");
    Check(list.RenderStateForTest(Root, a, 2, false, false, -1, 96, png(L"footer-hover-96.png").c_str()) && list.RenderStateForTest(Root, a, 2, false, false, -1, 192, png(L"footer-hover-192.png").c_str()), "board « états »: the footer hovered renders");
    Check(list.RenderStateForTest(Root, a, 0, true, false, -1, 96, png(L"cross-hover-96.png").c_str()) && list.RenderStateForTest(Root, a, 0, true, false, -1, 192, png(L"cross-hover-192.png").c_str()), "board « états »: the cross hovered renders");
    ValidationBubble::Bubble bubble;
    Check(bubble.RenderForTest(Root, Forms::InvalidText(L"missing", L"", L""), 192, png(L"bubble-missing-192.png").c_str()) && bubble.RenderForTest(Root, Forms::InvalidText(L"email-domain", L"", L""), 192, png(L"bubble-domain-192.png").c_str())
          && bubble.RenderForTest(Root, Forms::InvalidText(L"too-short", L"8", L"5"), 192, png(L"bubble-too-short-192.png").c_str()), "board « états »: three bubbles render (one of them on two lines)");
    Check(std::abs(bubble.HeightForTest() - 34) < .5f, "a one-line bubble stays 34 high");
    Check(bubble.RenderForTest(Root, Forms::InvalidText(L"email-at", L"", L""), 96, png(L"bubble-96.png").c_str()) && bubble.RenderForTest(Root, Forms::InvalidText(L"email-at", L"", L""), 192, png(L"bubble-192.png").c_str()), "the bubble renders offscreen");
    Check(std::abs(bubble.HeightForTest() - 34) < .5f && std::abs(bubble.WidthForTest() - 267) <= 2, "the bubble: about 267 × 34 for the board's text (Paper 1LJW-0: 266.6 × 33.6)");
    for (const wchar_t* f : {L"infos-96.png", L"merged-96.png", L"bubble-96.png"}) { const auto size = std::filesystem::exists(png(f)) ? std::filesystem::file_size(png(f)) : 0; Check(size > 1500, "a rendered surface is not blank (PNG has content)"); }
}

// Night 24/11 regression: a private window starts a fresh engine, so its first visit of the fixture shows the accepted
// certificate error as the « Non sécurisé » chip; the key pill was hidden behind any chip and vault_e2e (R-2) lost its
// key. As on main, the key stays available whenever the host says so: after the padlock, or after the chip's plate.
static void KeyPill(const std::wstring& out) {
    using namespace NativeChrome;
    for (UINT dpi : {96u, 144u}) {
        Chrome chrome; Check(SUCCEEDED(chrome.InitOffscreen(Root, dpi, 1180, 84)), "offscreen chrome for the key pill");
        const std::vector<std::wstring> titles{L"GitHub"};
        struct State { const wchar_t* name; const wchar_t* uri; bool cert; };
        // #127 (PR #139): https on this computer reads « Local » (the vault fixtures' https://127.0.0.1, certificate error
        // or not); another site with an accepted certificate error reads « Non sécurisé ».
        for (const State s : {State{L"secure", L"https://compte.exemple.test/login", false}, State{L"certificate", L"https://expired.exemple.test/login", true},
                              State{L"local", L"https://127.0.0.1:18447/login", false}, State{L"local-certificate", L"https://127.0.0.1:18447/login", true}}) {
            chrome.address = AddressBar::Describe(s.uri, s.cert); chrome.addressEditing = false; chrome.vaultKey = true; chrome.vaultKeyCount = 2;
            chrome.Paint(titles, 0, s.uri, false, 0, 0);
            const auto pill = chrome.VaultPillBox(); const float cx = (pill.left + pill.right) / 2, cy = (pill.top + pill.bottom) / 2;
            const bool shown = pill.right > pill.left && chrome.Hit(cx, cy) == Chrome::VaultKeyId;
            const bool local = chrome.address.security == AddressBar::Security::Local;
            Check(shown, local ? "the key pill shows right after « Local » (a project of this computer, certificate error or not)"
                         : s.cert ? "the key pill stays on a page whose certificate error was accepted (« Non sécurisé » chip), as the key did on main"
                                  : "the key pill shows after the padlock on a secure page");
            Check(local == (std::wstring(s.name).rfind(L"local", 0) == 0), "the fixture reads as the expected state (« Local » only for this computer)");
            Check(!shown || pill.left >= chrome.indicator.right, "the pill follows the indicator (padlock, chip plate or « Local »), never over it");
            if (chrome.address.security == AddressBar::Security::Local) {
                // « Local » (Paper « Barre du haut »): pill, 6, the 1 × 10 divider (#545454 in the #292929 field), 6, the
                // address. With the key pill it reads: « Local », 2, key, 6, divider, 6, address. Nothing under the key.
                const auto rgb = [&](float x) { return chrome.SurfacePixel(x, cy) & 0xFFFFFF; };
                const auto close = [](UINT32 p, UINT32 want) { for (int k = 0; k < 24; k += 8) if (std::abs(int((p >> k) & 255) - int((want >> k) & 255)) > 3) return false; return true; };
                const float d = std::ceil(pill.right) + Design::Address::localDivider;
                bool gap = true, after = true, text = false;
                for (float x = pill.right + .75f; x < d - .5f; x += .5f) gap = gap && close(rgb(x), 0x292929);
                for (float x = d + 1.75f; x < d + 1 + Design::Address::localDivider - .5f; x += .5f) after = after && close(rgb(x), 0x292929);
                for (float x = d + 1 + Design::Address::localDivider; x < d + 90; x += .5f) text = text || ((rgb(x) >> 8) & 255) > 0x70;
                Check(gap && close(rgb(d + .5f), 0x545454), "« Local »: the divider comes 6 DIP after the KEY pill (crisp #545454), nothing between them");
                Check(after && text, "« Local »: the address starts 6 DIP after that divider, never under the key pill");
            }
            Check(SUCCEEDED(chrome.SavePng((out + L"\\key-pill-" + s.name + L"-" + std::to_wstring(dpi) + L".png").c_str())), "key pill snapshot");
        }
        chrome.addressEditing = true; chrome.Paint(titles, 0, L"", true, 0, 'L');
        Check(chrome.VaultPillBox().right <= chrome.VaultPillBox().left, "while the address is edited the pill steps aside (the EDIT covers its lane)");
        chrome.addressEditing = false; chrome.vaultKey = false; chrome.Paint(titles, 0, L"", false, 0, 0);
        Check(chrome.VaultPillBox().right <= chrome.VaultPillBox().left, "no account for the site: no pill");
        chrome.Dispose();
    }
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Root = argc > 1 ? argv[1] : std::filesystem::current_path().wstring();
    const std::wstring out = argc > 2 ? argv[2] : Root + L"\\build\\form-memory";
    std::filesystem::create_directories(out);
    for (auto* name : NativeChrome::FontFiles) AddFontResourceExW((Root + L"\\assets\\fonts\\" + name).c_str(), FR_PRIVATE, nullptr);
    Values(); StoreTests(out + L"\\store"); EngineImport(out); Reports(); RowsTests(); Placement(); Words(); ScriptTests(out); Surfaces(out); KeyPill(out);
    std::printf("%s form memory / list under the field / validation bubble: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
    CoUninitialize();
    return failures ? 1 : 0;
}
