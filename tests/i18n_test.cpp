// Interface language core (src/i18n.h, src/i18n-catalog.inc, src/ui-language.h).
// Native only, temporary folders only. Compiled with -Wall -Wextra -Werror (run_all.py).
#include <windows.h>
#include <cstdio>
#include <set>
#include <string>
#include "../src/i18n.h"
#include "../src/ui-language.h"
#include "../src/settings.h"
#include "../src/history.h"
#include "../src/profile-color.h"

static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); }

static std::set<std::wstring> Placeholders(const wchar_t* s) {
    std::set<std::wstring> out;
    for (const wchar_t* p = s; *p; ++p) if (p[0] == L'{' && p[1] >= L'0' && p[1] <= L'9' && p[2] == L'}') out.insert(std::wstring(p, 3));
    return out;
}

int main() {
    using I18n::Locale;
    // Default: French, before anything sets it.
    Check(I18n::Current() == Locale::Fr, "default language is French");
    Check(std::wstring(TR(language_label)) == L"Langue", "TR in French by default");
    Check(std::wstring(I18n::Tag()) == L"fr-FR" && std::string(I18n::HtmlLang()) == "fr", "engine tag fr-FR, html lang fr");

    // Catalog: every message exists in both languages, with the same placeholders.
    bool full = true, same = true, named = true;
    for (std::size_t i = 0; i < I18n::Count; ++i) {
        const auto& e = I18n::EntryOf(static_cast<I18n::Id>(i));
        if (!e.fr || !e.en || !*e.fr || !*e.en) { full = false; std::printf("  empty: %s\n", e.name); }
        if (Placeholders(e.fr) != Placeholders(e.en)) { same = false; std::printf("  placeholders differ: %s\n", e.name); }
        if (!e.name || !*e.name) named = false;
    }
    Check(I18n::Count > 0 && full, "catalog: no empty French or English message");
    Check(same, "catalog: same {n} placeholders in both languages");
    Check(named, "catalog: every entry carries its id name");
    Check(std::wstring(I18n::EntryOf(I18n::Id::Count_).fr).empty(), "out-of-range id: empty, never a crash");

    // Formatting.
    Check(I18n::FormatText(L"{1} avant {0}", {L"a", L"b"}) == L"b avant a", "placeholders in any order");
    Check(I18n::FormatText(L"{0} {2}", {L"x"}) == L"x {2}", "missing argument stays visible");
    Check(I18n::FormatText(L"a {x} {} {0", {L"z"}) == L"a {x} {} {0", "other braces untouched");
    Check(I18n::Decimal(1.5, 1) == L"1,5" && I18n::Percent(100) == L"100 %", "French decimal comma, « 100 % »");
    Check(I18n::PluralOne(Locale::Fr, 0) && I18n::PluralOne(Locale::Fr, 1) && !I18n::PluralOne(Locale::Fr, 2), "French plural: 0 and 1 singular");
    Check(!I18n::PluralOne(Locale::En, 0) && I18n::PluralOne(Locale::En, 1) && !I18n::PluralOne(Locale::En, 2), "English plural: only 1 singular");
    Check(I18n::Utf8(L"Français — \U0001F600") == "Fran\xC3\xA7" "ais \xE2\x80\x94 \xF0\x9F\x98\x80", "UTF-8: 2, 3 and 4 byte sequences");
    Check(I18n::Utf8(std::wstring(1, static_cast<wchar_t>(0xD800))) == "\xEF\xBF\xBD", "UTF-8: lone surrogate replaced");

    // English, set explicitly (never the OS language nor the user's real preference).
    I18n::SetForProcess(Locale::En);
    Check(std::wstring(TR(language_label)) == L"Language" && std::wstring(TR(language_pending)) == L"Applies at next start.", "TR in English once set");
    Check(std::wstring(I18n::Tag()) == L"en-US" && std::string(I18n::HtmlLang()) == "en", "engine tag en-US, html lang en");
    Check(I18n::Decimal(1.5, 1) == L"1.5" && I18n::Percent(100) == L"100%", "English decimal point, « 100% »");
    Check(I18n::U8(I18n::Id::language_label) == "Language", "U8 follows the language");
    I18n::SetForProcess(Locale::Fr);
    Check(std::string(I18n::U8(I18n::Id::language_pending)) == "Au prochain d\xC3\xA9marrage.", "back to French");

    // Preference file: parse.
    using UiLanguage::Status;
    auto parse = [](const char* s) { return UiLanguage::Parse(s); };
    Check(parse("").status == Status::Missing && parse("").locale == Locale::Fr, "empty file: French, missing");
    Check(parse("browser-ui-language 1\nlanguage=en\n").locale == Locale::En && parse("browser-ui-language 1\nlanguage=en\n").status == Status::Ok, "language=en");
    Check(parse("browser-ui-language 1\r\nlanguage=fr\r\n").locale == Locale::Fr && parse("browser-ui-language 1\r\nlanguage=fr\r\n").status == Status::Ok, "CRLF, language=fr");
    Check(parse("\xEF\xBB\xBF" "browser-ui-language 1\nlanguage=en").locale == Locale::En, "UTF-8 BOM and no final newline");
    Check(parse("browser-ui-language 2\nlanguage=en\n").status == Status::UnknownVersion && parse("browser-ui-language 2\nlanguage=en\n").locale == Locale::Fr, "unknown version: French, reported");
    Check(parse("garbage\nlanguage=en\n").status == Status::Invalid && parse("garbage\nlanguage=en\n").locale == Locale::Fr, "foreign file: French, invalid");
    Check(parse("browser-ui-language 1\nlanguage=de\n").status == Status::Invalid && parse("browser-ui-language 1\nlanguage=de\n").locale == Locale::Fr, "unknown language: French, invalid");
    Check(parse("browser-ui-language 1\n").status == Status::Invalid, "header without a language: invalid");
    Check(parse("browser-ui-language 1\nlanguage=en\nlanguage=xx\n").locale == Locale::Fr, "last language line wins, even when invalid");
    Check(UiLanguage::Parse(UiLanguage::Format(Locale::En)).locale == Locale::En && UiLanguage::Parse(UiLanguage::Format(Locale::Fr)).locale == Locale::Fr, "Format round-trips");
    Check(std::wstring(UiLanguage::Autonym(Locale::Fr)) == L"Français" && std::wstring(UiLanguage::Autonym(Locale::En)) == L"English", "autonyms");

    // Preference file: disk (temporary folder only).
    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp) + L"browser-i18n-test-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    Check(UiLanguage::Load(dir).status == Status::Missing && UiLanguage::Load(dir).locale == Locale::Fr, "no file: French");
    Check(UiLanguage::Save(dir, Locale::En), "save English");
    Check(UiLanguage::Load(dir).locale == Locale::En && UiLanguage::Load(dir).status == Status::Ok, "reload: English");
    Check(GetFileAttributesW((dir + L"\\ui-language.tmp" + std::to_wstring(GetCurrentProcessId())).c_str()) == INVALID_FILE_ATTRIBUTES, "no temp file left behind");
    Check(UiLanguage::Save(dir, Locale::Fr) && UiLanguage::Load(dir).locale == Locale::Fr, "save French again: reload French");
    Check(!UiLanguage::Save(dir + L"\\missing\\deeper", Locale::En), "save into a missing folder: false, never « saved »");
    // A folder in place of the file: reading fails as unreadable, saving fails.
    const std::wstring blocked = dir + L"\\blocked"; CreateDirectoryW(blocked.c_str(), nullptr); CreateDirectoryW((blocked + L"\\ui-language").c_str(), nullptr);
    Check(UiLanguage::Load(blocked).status == Status::Unreadable && UiLanguage::Load(blocked).locale == Locale::Fr, "unreadable preference: French, reported");
    Check(!UiLanguage::Save(blocked, Locale::En), "save over a folder: false");
    RemoveDirectoryW((blocked + L"\\ui-language").c_str()); RemoveDirectoryW(blocked.c_str());
    DeleteFileW(UiLanguage::PathIn(dir).c_str()); RemoveDirectoryW(dir.c_str());

    // Formats of the converted models, both languages (fixed inputs: never the clock or the OS language).
    {
        namespace DL = NativeChrome::Downloads; namespace BS = BrowserSettings; namespace HV = NativeChrome::HistoryView;
        const std::int64_t MB = 1024 * 1024;
        Check(DL::Bytes(504 * MB / 10, 200 * MB) == L"50,4/200 Mo" && DL::Bytes(512, 0) == L"512 o", "French sizes: decimal comma, French units");
        Check(DL::FolderLabel(L"C:\\Users\\a\\Downloads\\f.zip") == L"Dans …/Downloads" && DL::FailureText(L"") == L"Échec", "French download labels");
        Check(std::wstring(DL::PeriodLabel(DL::Period::Yesterday)) == L"Hier" && std::wstring(DL::TipFor(DL::Action::ShowInFolder)) == L"Afficher dans le dossier", "French periods and actions");
        Check(BS::SleepLabel(30) == L"Après 30 min" && BS::RetentionLabel(1) == L"1 jour" && BS::RetentionLabel(30) == L"30 jours" && BS::ZoomLabel(110) == L"110 %", "French settings labels and plurals");
        Check(BS::PermissionValue(BS::Permission::Camera, BS::PermissionState::Allow) == L"Autorisée" && BS::PermissionValue(BS::Permission::Notifications, BS::PermissionState::Block) == L"Bloquées" &&
              BS::PermissionValue(BS::Permission::Microphone, BS::PermissionState::Allow) == L"Autorisé", "French agreement of permission values");
        const std::int64_t day = 86400000LL, now = 20070LL * day;   // 2024-12-13 (UTC), a fixed date
        Check(HV::DateText(now - 3 * day, now, 0) == L"10 déc." && HV::DateText(now - 400 * day, now, 0) == L"9 nov. 2023", "French dates: « 10 déc. », « 9 nov. 2023 »");
        Check(HV::EmptyText(L" rust ") == L"Aucun résultat pour « rust »", "French history empty state");
        I18n::SetForProcess(Locale::En);
        Check(DL::Bytes(504 * MB / 10, 200 * MB) == L"50.4/200MB" && DL::Bytes(512, 0) == L"512B", "English sizes: the Paper copy « 50.4/200MB »");
        Check(DL::FolderLabel(L"C:\\Users\\a\\Downloads\\f.zip") == L"In …/Downloads" && DL::FailureText(L"Disk full") == L"Failed · Disk full", "English download labels");
        Check(BS::SleepLabel(120) == L"After 2 h" && BS::RetentionLabel(1) == L"1 day" && BS::RetentionLabel(7) == L"7 days" && BS::ZoomLabel(110) == L"110%", "English settings labels and plurals");
        Check(std::wstring(BS::Info(BS::Tracking::Balanced).label) == L"Balanced" && std::string(BS::Info(BS::Tracking::Balanced).key) == "balanced", "a choice's label follows the language, its stored key never does");
        Check(BS::PermissionValue(BS::Permission::Camera, BS::PermissionState::Allow) == L"Allowed", "English permission value");
        Check(HV::DateText(now - 3 * day, now, 0) == L"Dec 10" && HV::DateText(now - 400 * day, now, 0) == L"Nov 9, 2023", "English dates: « Dec 10 », « Nov 9, 2023 »");
        Check(HV::EmptyText(L"") == L"No history yet" && HV::EmptyText(L"rust") == L"No results for \u201crust\u201d", "English history empty states (Paper copy)");
        I18n::SetForProcess(Locale::Fr);
        Check(std::wstring(BS::Info(BS::Tracking::Balanced).label) == L"\u00c9quilibr\u00e9", "the same table answers in French again: nothing frozen");
    }

    // Address bar placeholder, engine language argument, spoken names (both languages).
    {
        Check(std::wstring(TR(chrome_address_placeholder)) == L"Rechercher ou saisir une adresse", "French address placeholder");
        Check(I18n::WithEngineLang(L"") == L"--lang=fr" && I18n::WithEngineLang(L"--foo") == L"--foo --lang=fr", "French engine argument --lang=fr");
        Check(I18n::WithEngineLang(L"--lang=de --x") == L"--lang=de --x", "a --lang= passed by the person is kept");
        Check(std::wstring(ProfileColor::HueName(0)) == L"rouge" && std::wstring(ProfileColor::HueName(150)) == L"\u00e9meraude", "French hue names");
        Check(std::wstring(TR(key_shift_enter)) == L"Maj+Entr\u00e9e" && std::wstring(TR(key_escape)) == L"\u00c9chap", "French key names");
        Check(TRP(bookmarks_folder_summary, 1) == L"Dossier, 1 \u00e9l\u00e9ment" && TRP(bookmarks_folder_summary, 3) == L"Dossier, 3 \u00e9l\u00e9ments" && TRP(bookmarks_results, 0) == L"0 r\u00e9sultat", "French folder summary and result count");
        I18n::SetForProcess(Locale::En);
        Check(std::wstring(TR(chrome_address_placeholder)) == L"Search, browse, whatever", "English address placeholder: the Paper copy");
        Check(I18n::WithEngineLang(L"") == L"--lang=en-US" && I18n::WithEngineLang(L"--foo") == L"--foo --lang=en-US", "English engine argument --lang=en-US (never --lang=fr beside Language=en-US)");
        Check(std::wstring(ProfileColor::HueName(0)) == L"red" && std::wstring(ProfileColor::HueName(150)) == L"emerald", "English hue names");
        Check(std::wstring(TR(key_shift_enter)) == L"Shift+Enter" && std::wstring(TR(key_escape)) == L"Esc" && std::wstring(TR(key_enter)) == L"Enter", "English key names");
        Check(TRP(bookmarks_folder_summary, 1) == L"Folder, 1 item" && TRP(bookmarks_results, 0) == L"0 results", "English folder summary and result count");
        Check(TRF(settings_announce_disconnected, L"Claude Desktop") == L"Claude Desktop disconnected" && std::wstring(TR(annotation_reopened)) == L"Annotation reopened", "English annotation announcements");
        I18n::SetForProcess(Locale::Fr);
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
