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
// Built during static initialization, before any language is pinned (as main.cpp's global `chrome`):
// none of its tooltips may be frozen in the default French.
static Chrome earlyChrome;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static const SettingsUI::Slot* Hit(const std::vector<SettingsUI::Slot>& slots, int hit) { for (auto& s : slots) if (s.hit == hit) return &s; return nullptr; }
static const Accessibility::Node* Node(const std::vector<Accessibility::Node>& nodes, int hit) { const long long key = SettingsUI::Drawer::UiaKey(hit); for (auto& n : nodes) if (n.key == key) return &n; return nullptr; }

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: language_settings_test <repo root> <capture folder>\n"); return 2; }
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // CI pipes stdout: a crash must not swallow the checks before it
    // An apartment of our own, as main.cpp and the other UI fixtures: without it the WIC objects live in the
    // implicit MTA, which ends with the system's worker threads, and releasing them then crashes (CI runner).
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::printf("FAIL native COM initialization\n"); return 1; }
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
    Check(ContextMenu::Menu::KeysForTest(L"Ctrl+Shift+I") == std::vector<std::wstring>{L"Ctrl", L"Maj", L"I"} &&
          ContextMenu::Menu::KeysForTest(TR(key_ctrl_shift_t)) == std::vector<std::wstring>{L"Ctrl", L"Maj", L"T"}, "French: context menu caps « Ctrl » « Maj » for the engine's « Shift » too");
    // The engine names arrows in words (« Alt+Flèche gauche », « Alt+Left Arrow »): the caps show the glyph.
    Check(ContextMenu::Menu::KeysForTest(L"Alt+Flèche gauche") == std::vector<std::wstring>{L"Alt", L"←"} &&
          ContextMenu::Menu::KeysForTest(L"Alt+Flèche droite") == std::vector<std::wstring>{L"Alt", L"→"} &&
          ContextMenu::Menu::KeysForTest(L"Alt+Fleche vers la gauche") == std::vector<std::wstring>{L"Alt", L"←"} &&
          ContextMenu::Menu::KeysForTest(L"Ctrl+Flèche haut") == std::vector<std::wstring>{L"Ctrl", L"↑"} &&
          ContextMenu::Menu::KeysForTest(L"Ctrl+Flèche bas") == std::vector<std::wstring>{L"Ctrl", L"↓"} &&
          ContextMenu::Menu::KeysForTest(L"Alt+Left Arrow") == std::vector<std::wstring>{L"Alt", L"←"} &&
          ContextMenu::Menu::KeysForTest(L"Alt+Right arrow") == std::vector<std::wstring>{L"Alt", L"→"} &&
          ContextMenu::Menu::KeysForTest(L"Alt+Arrow Left") == std::vector<std::wstring>{L"Alt", L"←"} &&
          ContextMenu::Menu::KeysForTest(L"Alt+Left") == std::vector<std::wstring>{L"Alt", L"←"} &&
          ContextMenu::Menu::KeysForTest(L"Ctrl+Page down") == std::vector<std::wstring>{L"Ctrl", L"Page down"},
          "context menu caps: the engine's arrow names (« Flèche gauche », « Left Arrow ») become ← → ↑ ↓, other words stay");
    // Two windows share the browser-wide preference. Drawer A (dr) last saw « Français »; drawer B saves
    // English while A stays open. A's menu must show what is on disk now, and A's explicit « Français »
    // must be written (it was ignored before: chosen == A's stale cache, zero saves, disk stayed English).
    {
        BrowserSettings::State bs; SettingsUI::Drawer b; Check(b.Create(hi, owner, root, &bs, hooks()), "second drawer (another window) created");
        open(b); choice = 2; b.ActivateForTest(SettingsUI::LanguageHit, false); b.SettleForTest();
        Check(UiLanguage::Load(data).locale == Locale::En, "two drawers: B saved English");
        const int before = saves; menu.clear(); choice = 1; dr.ActivateForTest(SettingsUI::LanguageHit, false); dr.SettleForTest();
        Check(menu.size() == 2 && !menu[0].checked && menu[1].checked, "two drawers: A's menu checks English, as on disk now (not A's stale Français)");
        Check(saves == before + 1 && UiLanguage::Load(data).locale == Locale::Fr, "two drawers: A's explicit « Français » is saved, disk says Français");
        const auto* row = Hit(dr.SlotsForTest(), SettingsUI::LanguageHit);
        Check(row && row->value == L"Français" && row->lines.empty() && !row->error, "two drawers: A shows Français, no note");
        b.Hide(false, false); b.Dispose();
    }
    // The file changes while A's menu is open (another window saves English then): the choice still wins.
    {
        const int before = saves; choice = 1;
        SettingsUI::Drawer::Hooks h = hooks();
        h.choose = [&](const std::vector<ContextMenu::Item>& items, RECT, bool) { menu = items; UiLanguage::Save(data, Locale::En); return choice; };
        BrowserSettings::State cs; SettingsUI::Drawer c; Check(c.Create(hi, owner, root, &cs, h), "third drawer created");
        open(c); c.ActivateForTest(SettingsUI::LanguageHit, false); c.SettleForTest();
        Check(menu.size() == 2 && menu[0].checked, "menu open: Français checked (on disk when it opened)");
        Check(saves == before + 1 && UiLanguage::Load(data).locale == Locale::Fr, "external English save while the menu was open: the explicit « Français » is written");
        // A failure in that situation keeps what is on disk (English) and says so.
        failSave = true; const int failedBefore = saves; c.ActivateForTest(SettingsUI::LanguageHit, false); c.SettleForTest(); failSave = false;
        const auto* row = Hit(c.SlotsForTest(), SettingsUI::LanguageHit);
        Check(saves == failedBefore + 1 && UiLanguage::Load(data).locale == Locale::En && row && row->value == L"English" && row->error, "failed save after an external change: shows the disk value (English) and the error");
        c.Hide(false, false); c.Dispose();
        UiLanguage::Save(data, Locale::Fr);
    }
    Check(std::wstring(TR(settings_title)) == L"Réglages", "after all saves the process language is still French (pinned)");
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
    {   // A global chrome built before the language was pinned: its toolbar tooltips follow the pinned language.
        Check(SUCCEEDED(earlyChrome.InitOffscreen(root, 96, 1180, 84)), "global chrome (built before the pin) initialised");
        earlyChrome.audioShown = true; earlyChrome.annotateShown = true; earlyChrome.Build(1, 0);
        const wchar_t* audio = nullptr; const wchar_t* annotate = nullptr;
        for (const auto& r : earlyChrome.regions) { if (r.id == 'V') audio = r.tip; if (r.id == 'N') annotate = r.tip; }
        Check(audio && std::wstring(audio) == L"Site sound", "English: the site sound tooltip is « Site sound » (was frozen « Son des sites »)");
        Check(annotate && std::wstring(annotate) == L"Annotate (Ctrl+Shift+A)", "English: the Annotate tooltip before any annotation sync");
        const auto caps = ContextMenu::Menu::KeysForTest(TR(key_ctrl_shift_t));
        Check(caps == std::vector<std::wstring>{L"Ctrl", L"Shift", L"T"}, "English: context menu caps « Ctrl » « Shift » « T » (never « Maj »)");
        Check(ContextMenu::Menu::KeysForTest(L"Ctrl+Shift+I") == std::vector<std::wstring>{L"Ctrl", L"Shift", L"I"}, "English: the engine's own « Ctrl+Shift+I » keeps « Shift »");
    }
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
    earlyChrome.Dispose();   // Direct2D/WIC released before CoUninitialize, as main.cpp disposes its global chrome
    CoUninitialize();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
