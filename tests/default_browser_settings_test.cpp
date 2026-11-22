// Réglages › « Navigateur par défaut » (src/settings-panel.h) on the real drawer, with SYNTHETIC hooks: the row is
// shown only when this copy is registered with Windows, reads « Défini » when Browser is the default and
// « Définir par défaut » otherwise, opens Windows' Default apps page (the hook) and never changes anything itself,
// follows a change made in Windows when the window comes back, is found by the settings search, and names itself
// for UI Automation. No Paper frame exists for this row: captures (96 / 144 dpi, FR and EN) go to <capture folder>
// for the designer. Nothing on the machine is read or written (the hooks are fakes).
// Usage: default_browser_settings_test.exe <repo root> <capture folder>
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
    if (argc < 3) { std::printf("usage: default_browser_settings_test <repo root> <capture folder>\n"); return 2; }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::printf("FAIL native COM initialization\n"); return 1; }
    const std::wstring root = argv[1], shots = argv[2];
    CreateDirectoryW(shots.c_str(), nullptr);
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"DefaultBrowserSettingsOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_POPUP | WS_VISIBLE, 100, 100, 1100, 860, nullptr, nullptr, hi, nullptr);

    int status = 0, opened = 0;   // the fake Windows answer; how many times the Default apps page was asked for
    auto hooks = [&] {
        SettingsUI::Drawer::Hooks h;
        h.profiles = [] { SettingsUI::ProfileItem a; a.id = L"default"; a.name = L"Personnel"; a.current = true; return std::vector<SettingsUI::ProfileItem>{a}; };
        h.savedLanguage = [] { return I18n::Current(); };
        h.saveLanguage = [](I18n::Locale) { return true; };
        h.defaultBrowser = [&] { return status; };
        h.setDefaultBrowser = [&] { ++opened; };
        return h;
    };
    auto open = [&](SettingsUI::Drawer& dr) { dr.ShowProfiles(); dr.SettleForTest(); dr.KeyForTest(VK_ESCAPE); dr.SettleForTest(); };
    auto shot = [&](SettingsUI::Drawer& dr, const std::wstring& name) {
        dr.ScrollForTest(0); dr.SettleForTest();
        const bool a = dr.RenderForTest(shots + L"\\" + name + L"-96.png", 96, 731, root), b = dr.RenderForTest(shots + L"\\" + name + L"-144.png", 144, 731, root);
        Check(a && b, "capture rendered (96 and 144 dpi)");
    };

    BrowserSettings::State st;
    {   // Not the default.
        status = 0; SettingsUI::Drawer dr; Check(dr.Create(hi, owner, root, &st, hooks()), "drawer created"); open(dr);
        const auto slots = dr.SlotsForTest(); const auto* row = Hit(slots, SettingsUI::DefaultBrowserHit);
        const auto* lang = Hit(slots, SettingsUI::LanguageHit); const auto* tabs = Hit(slots, SettingsUI::CatTabs);
        Check(row && row->kind == Kind::Nav && row->text == L"Navigateur par défaut" && row->value == L"Définir" && row->path == SettingsUI::BrowserGlyph,
              "not the default: « Navigateur par défaut · Définir », browser glyph (the Profils / Langue row pattern)");
        Check(row && lang && tabs && row->top - lang->top == SettingsUI::Layout::pitch && row->top < tabs->top && row->height == SettingsUI::Layout::row, "right under Langue, in the first group, one line");
        const auto* n = Node(dr.AccessibleNodesForTest(), SettingsUI::DefaultBrowserHit);
        Check(n && n->name == L"Navigateur par défaut" && n->status == L"Définir" && n->automationId == L"SettingsDefaultBrowser" &&
              n->help == L"Ouvre les Paramètres Windows, où vous choisissez Browser comme navigateur par défaut", "UIA: name, status, automation id, help says where it leads");
        shot(dr, L"fr-not-default");
        dr.ActivateForTest(SettingsUI::DefaultBrowserHit, false); dr.SettleForTest();
        Check(opened == 1, "click: Windows' Default apps page asked for, once (nothing changed by the drawer itself)");
        dr.ActivateForTest(SettingsUI::DefaultBrowserHit, true); dr.SettleForTest();
        Check(opened == 2, "keyboard (Entrée / Espace): same action");
        // The person chose Browser in Windows, then came back: the row follows.
        status = 1; dr.RefreshDefaultBrowser(); dr.SettleForTest();
        const auto* now = Hit(dr.SlotsForTest(), SettingsUI::DefaultBrowserHit);
        Check(now && now->value == L"Défini" && dr.DefaultBrowserForTest() == 1, "back from Windows as the default: « Défini »");
        bool found = false; for (const auto& r : SettingsUI::SearchSettings(L"par défaut", false, true, true)) found = found || r.entry->hit == SettingsUI::DefaultBrowserHit;
        Check(found, "search « par défaut » finds the row");
        found = false; for (const auto& r : SettingsUI::SearchSettings(L"liens", false, true, true)) found = found || r.entry->hit == SettingsUI::DefaultBrowserHit;
        Check(found, "search « liens » (keyword) finds it");
        found = false; for (const auto& r : SettingsUI::SearchSettings(L"par défaut", false, true, false)) found = found || r.entry->hit == SettingsUI::DefaultBrowserHit;
        Check(!found, "no row (copy not registered): the search does not offer it");
        dr.Hide(false, false); dr.Dispose();
    }
    {   // Already the default.
        status = 1; opened = 0; SettingsUI::Drawer dr; dr.Create(hi, owner, root, &st, hooks()); open(dr);
        const auto* row = Hit(dr.SlotsForTest(), SettingsUI::DefaultBrowserHit);
        Check(row && row->value == L"Défini", "the default: « Défini »");
        shot(dr, L"fr-default");
        dr.Hide(false, false); dr.Dispose();
    }
    {   // A copy Windows does not know (portable, dev): no row; no hook: no row.
        status = -1; SettingsUI::Drawer dr; dr.Create(hi, owner, root, &st, hooks()); open(dr);
        Check(!Hit(dr.SlotsForTest(), SettingsUI::DefaultBrowserHit), "not registered with Windows: no row");
        dr.Hide(false, false); dr.Dispose();
        SettingsUI::Drawer::Hooks h = hooks(); h.defaultBrowser = nullptr; SettingsUI::Drawer plain; plain.Create(hi, owner, root, &st, h); open(plain);
        Check(!Hit(plain.SlotsForTest(), SettingsUI::DefaultBrowserHit), "no hook: no row"); plain.Hide(false, false); plain.Dispose();
    }
    I18n::SetForProcess(I18n::Locale::En);
    for (int s : {0, 1}) {
        status = s; SettingsUI::Drawer dr; dr.Create(hi, owner, root, &st, hooks()); open(dr);
        const auto* row = Hit(dr.SlotsForTest(), SettingsUI::DefaultBrowserHit);
        Check(row && row->text == L"Default browser" && row->value == (s ? L"Set" : L"Make default"), s ? "English: « Default browser · Set »" : "English: « Default browser · Make default »");
        shot(dr, s ? L"en-default" : L"en-not-default");
        dr.Hide(false, false); dr.Dispose();
    }
    I18n::SetForProcess(I18n::Locale::Fr);
    DestroyWindow(owner);
    CoUninitialize();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
