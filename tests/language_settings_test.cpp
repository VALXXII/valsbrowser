// Réglages › « Langue » (src/settings-panel.h, src/ui-language.h, Paper p-K-0) on the real drawer:
// the row under Profils, its two-autonym menu (saved choice checked), « Au prochain démarrage. » while
// the saved language differs from the process language, an honest error when the save fails (value
// unchanged, danger ink), the bilingual search, the UIA view, and the same drawer in an English process.
// The preference is written ONLY to a temporary folder. Captures (96 / 144 dpi) go to <capture folder>.
// Usage: language_settings_test.exe <repo root> <capture folder>
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/chrome.h"
#include "../src/settings-panel.h"

using namespace NativeChrome;
using SettingsUI::Kind;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static const SettingsUI::Slot* Hit(const std::vector<SettingsUI::Slot>& slots, int hit) { for (auto& s : slots) if (s.hit == hit) return &s; return nullptr; }
static const Accessibility::Node* Node(const std::vector<Accessibility::Node>& nodes, int hit) { const long long key = SettingsUI::Drawer::UiaKey(hit); for (auto& n : nodes) if (n.key == key) return &n; return nullptr; }

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: language_settings_test <repo root> <capture folder>\n"); return 2; }
    const std::wstring root = argv[1], shots = argv[2];
    CreateDirectoryW(shots.c_str(), nullptr);
    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
    const std::wstring data = std::wstring(temp) + L"browser-language-settings-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(data.c_str(), nullptr);
    using I18n::Locale;

    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"LanguageSettingsOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_POPUP | WS_VISIBLE, 100, 100, 1100, 860, nullptr, nullptr, hi, nullptr);

    bool failSave = false; int saves = 0; int choice = 0; std::vector<ContextMenu::Item> menu;
    auto hooks = [&] {
        SettingsUI::Drawer::Hooks h;
        h.profiles = [] { SettingsUI::ProfileItem a; a.id = L"default"; a.name = L"Personnel"; a.current = true; return std::vector<SettingsUI::ProfileItem>{a}; };
        h.savedLanguage = [&] { return UiLanguage::Load(data).locale; };
        h.saveLanguage = [&](Locale l) { ++saves; return !failSave && UiLanguage::Save(data, l); };
        h.choose = [&](const std::vector<ContextMenu::Item>& items, RECT, bool) { menu = items; return choice; };
        return h;
    };
    auto open = [&](SettingsUI::Drawer& dr) { dr.ShowProfiles(); dr.SettleForTest(); dr.KeyForTest(VK_ESCAPE); dr.SettleForTest(); };
    auto shot = [&](SettingsUI::Drawer& dr, const wchar_t* name) {
        dr.ScrollForTest(0); dr.SettleForTest();
        const bool a = dr.RenderForTest(shots + L"\\" + name + L"-96.png", 96, 731, root), b = dr.RenderForTest(shots + L"\\" + name + L"-144.png", 144, 731, root);
        Check(a && b, "capture rendered (96 and 144 dpi)");
    };

    // ---- French process (default), no preference yet -------------------------------------------
    BrowserSettings::State st;
    SettingsUI::Drawer dr; Check(dr.Create(hi, owner, root, &st, hooks()), "drawer created");
    open(dr);
    {
        const auto slots = dr.SlotsForTest(); const auto* row = Hit(slots, SettingsUI::LanguageHit);
        const auto* prof = Hit(slots, SettingsUI::CatProfiles); const auto* tabs = Hit(slots, SettingsUI::CatTabs);
        Check(row && row->kind == Kind::Value && row->text == L"Langue" && row->value == L"Français" && row->path == SettingsUI::TranslateGlyph, "root: « Langue · Français » with the translate glyph");
        Check(row && prof && tabs && row->top > prof->top && row->top < tabs->top && row->top - prof->top == SettingsUI::Layout::pitch, "root: right under Profils, in the first group");
        Check(row && row->lines.empty() && row->height == SettingsUI::Layout::row && !row->error, "root: one line, no note while the saved language is this window's");
    }
    shot(dr, L"fr-root");
    // The menu: both autonyms, the saved one checked; English chosen and saved.
    choice = 2; dr.ActivateForTest(SettingsUI::LanguageHit, false); dr.SettleForTest();
    Check(menu.size() == 2 && menu[0].label == L"Français" && menu[1].label == L"English" && menu[0].checked && !menu[1].checked, "menu: Français (checked), English");
    Check(saves == 1 && UiLanguage::Load(data).locale == Locale::En, "English saved to the browser-wide preference");
    {
        const auto* row = Hit(dr.SlotsForTest(), SettingsUI::LanguageHit);
        Check(row && row->value == L"English" && row->lines.size() == 1 && row->lines[0] == L"Au prochain démarrage." && !row->error, "pending: « English » + « Au prochain démarrage. »");
        Check(row && row->height == SettingsUI::Layout::TwoLine(1), "pending: two-line row (56 DIP)");
        const auto* n = Node(dr.AccessibleNodesForTest(), SettingsUI::LanguageHit);
        Check(n && n->name == L"Langue" && n->status == L"English" && n->help == L"Au prochain démarrage." && n->expand == 1, "UIA: « Langue », value English, the note as help, opens a list");
        Check(std::wstring(TR(settings_title)) == L"Réglages", "the open window stays in French (no half switch)");
    }
    shot(dr, L"fr-pending-en");
    menu.clear(); choice = 0; dr.ActivateForTest(SettingsUI::LanguageHit, true);
    Check(menu.size() == 2 && !menu[0].checked && menu[1].checked && saves == 1, "menu reopened: English checked, dismissing saves nothing");
    // A failed save: the value stays, the error is said (never « saved »).
    failSave = true; choice = 1; dr.ActivateForTest(SettingsUI::LanguageHit, false); dr.SettleForTest();
    {
        const auto* row = Hit(dr.SlotsForTest(), SettingsUI::LanguageHit);
        Check(saves == 2 && UiLanguage::Load(data).locale == Locale::En, "failed save: the preference on disk is unchanged");
        Check(row && row->value == L"English" && row->error && row->lines.size() == 1 && row->lines[0] == L"Impossible d'enregistrer la langue.", "failed save: value unchanged, error note in danger ink");
    }
    shot(dr, L"fr-save-failed");
    failSave = false; choice = 1; dr.ActivateForTest(SettingsUI::LanguageHit, false); dr.SettleForTest();
    {
        const auto* row = Hit(dr.SlotsForTest(), SettingsUI::LanguageHit);
        Check(row && row->value == L"Français" && row->lines.empty() && !row->error && UiLanguage::Load(data).locale == Locale::Fr, "back to Français: saved, note and error gone");
    }
    // Search: the row answers in both languages, in French here.
    {
        const auto en = SettingsUI::SearchSettings(L"english"), fr = SettingsUI::SearchSettings(L"langue");
        Check(!en.empty() && en[0].entry->hit == SettingsUI::LanguageHit && std::wstring(en[0].entry->title) == L"Langue" && std::wstring(en[0].entry->path) == L"Réglages", "search « english »: Langue · Réglages");
        Check(!fr.empty() && fr[0].entry->hit == SettingsUI::LanguageHit && fr[0].at == 0, "search « langue »: title match");
    }
    dr.Hide(false, false); dr.Dispose();
    {   // No save hook (a host without the preference): no row.
        SettingsUI::Drawer plain; SettingsUI::Drawer::Hooks ph; ph.profiles = hooks().profiles; BrowserSettings::State ps;
        Check(plain.Create(hi, owner, root, &ps, ph), "drawer without the preference hooks"); open(plain);
        Check(!Hit(plain.SlotsForTest(), SettingsUI::LanguageHit), "no save hook: no « Langue » row"); plain.Hide(false, false); plain.Dispose();
    }

    // ---- English process (set explicitly), French saved: pending the other way ---------------------
    I18n::SetForProcess(Locale::En);
    SettingsUI::Drawer en; BrowserSettings::State es; Check(en.Create(hi, owner, root, &es, hooks()), "English drawer created");
    open(en);
    {
        const auto slots = en.SlotsForTest(); const auto* row = Hit(slots, SettingsUI::LanguageHit);
        Check(std::wstring(SettingsUI::Title(SettingsUI::View::Main)) == L"Settings", "English: « Settings »");
        Check(row && row->text == L"Language" && row->value == L"Français" && row->lines.size() == 1 && row->lines[0] == L"Applies at next start.", "English: « Language · Français » + « Applies at next start. »");
        const auto* tabs = Hit(slots, SettingsUI::CatTabs); const auto* prof = Hit(slots, SettingsUI::CatProfiles);
        Check(tabs && tabs->text == L"Tabs" && tabs->value == L"Restored" && prof && prof->text == L"Profiles" && prof->value == L"Personnel", "English: Tabs · Restored, Profiles · Personnel (a name is data)");
        const auto lang = SettingsUI::SearchSettings(L"lang");
        Check(!lang.empty() && lang[0].entry->hit == SettingsUI::LanguageHit && std::wstring(lang[0].entry->title) == L"Language" && std::wstring(lang[0].entry->path) == L"Settings", "English search « lang »: Language · Settings");
        const auto fr = SettingsUI::SearchSettings(L"francais"); bool found = false; for (auto& r : fr) found = found || r.entry->hit == SettingsUI::LanguageHit;
        Check(found, "English search « francais » still finds Language");
    }
    shot(en, L"en-pending-fr");
    choice = 2; en.ActivateForTest(SettingsUI::LanguageHit, false); en.SettleForTest();
    { const auto* row = Hit(en.SlotsForTest(), SettingsUI::LanguageHit); Check(row && row->value == L"English" && row->lines.empty(), "English chosen in an English window: no note"); }
    shot(en, L"en-root");
    en.Hide(false, false); en.Dispose();
    I18n::SetForProcess(Locale::Fr);

    DeleteFileW(UiLanguage::PathIn(data).c_str()); RemoveDirectoryW(data.c_str());
    DestroyWindow(owner);
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
