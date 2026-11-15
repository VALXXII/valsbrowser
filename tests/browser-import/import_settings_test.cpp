// Settings wiring of « Importer depuis un navigateur… » next to the password manager entry (review 934ef4b
// finding 1). Two parts:
//  1. main.cpp, comments stripped: the drawer hooks `importData` AND `vaultInfo` are both executable statements
//     (inserting the import hook once commented out `vaultInfo`, which hid « Gérer les mots de passe »).
//  2. The real drawer with SYNTHETIC hooks only (no vault is opened, no credential read): with vaultInfo saying
//     « active, 3 accounts » Confidentialité shows both rows; without it, no password row; without importData,
//     no import row and no search result for it (a private window).
// Usage: import_settings_test.exe <repo root>
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../../src/chrome.h"
#include "../../src/settings-panel.h"

using namespace NativeChrome;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static const SettingsUI::Slot* Hit(const std::vector<SettingsUI::Slot>& slots, int hit) { for (auto& s : slots) if (s.hit == hit) return &s; return nullptr; }

// C++ source without comments (string and character literals kept intact).
static std::string Code(const std::string& src) {
    std::string out; out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        if (c == '"' || c == '\'') {
            if (c == '"' && i > 0 && src[i - 1] == 'R') { const auto open = src.find('(', i); const std::string close = ")" + src.substr(i + 1, open - i - 1) + "\""; const auto end = src.find(close, open); out += src.substr(i, end + close.size() - i); i = end + close.size() - 1; continue; }
            out += c; for (++i; i < src.size() && src[i] != c; ++i) { out += src[i]; if (src[i] == '\\' && i + 1 < src.size()) out += src[++i]; } if (i < src.size()) out += src[i]; continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') { while (i < src.size() && src[i] != '\n') ++i; out += '\n'; continue; }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') { const auto end = src.find("*/", i + 2); i = end == std::string::npos ? src.size() : end + 1; out += ' '; continue; }
        out += c;
    }
    return out;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("usage: import_settings_test <repo root>\n"); return 2; }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::printf("FAIL COM\n"); return 1; }
    const std::wstring root = argv[1];

    // ---- 1. main.cpp wiring --------------------------------------------------------------------------------
    {
        std::ifstream in(std::filesystem::path(root) / L"src" / L"main.cpp", std::ios::binary); std::stringstream b; b << in.rdbuf();
        const std::string code = Code(b.str());
        Check(!b.str().empty(), "main.cpp read");
        Check(code.find("hooks.vaultInfo=[]{return VaultInfo();};") != std::string::npos, "main.cpp: hooks.vaultInfo is executable code, not inside a comment");
        Check(code.find("hooks.importData=[](bool keyboard){ShowImportDialog(keyboard);};") != std::string::npos, "main.cpp: hooks.importData is executable code");
        Check(code.find("hooks.managePasswords=[](bool keyboard){ShowVaultManager(keyboard);};") != std::string::npos, "main.cpp: hooks.managePasswords still wired");
    }

    // ---- 2. Drawer rows from synthetic hooks -----------------------------------------------------------------
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"ImportSettingsOwner"; RegisterClassW(&wc);
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"owner", WS_POPUP | WS_VISIBLE, 100, 100, 1100, 860, nullptr, nullptr, hi, nullptr);
    std::vector<std::unique_ptr<SettingsUI::Drawer>> keep;   // alive until exit: their windows belong to `owner`
    auto privacy = [&](bool vault, bool import) {
        SettingsUI::Drawer::Hooks h;
        if (vault) h.vaultInfo = [] { return std::pair<bool, std::size_t>{true, 3}; };   // synthetic: « active, 3 accounts »
        if (import) h.importData = [](bool) {};
        BrowserSettings::State st;
        auto dr = std::make_unique<SettingsUI::Drawer>(); dr->Create(hi, owner, root, &st, std::move(h));
        dr->ShowProfiles(); dr->SettleForTest(); dr->KeyForTest(VK_ESCAPE); dr->SettleForTest();
        dr->ActivateForTest(SettingsUI::CatPrivacy, false); dr->SettleForTest();
        auto slots = dr->SlotsForTest(); keep.push_back(std::move(dr));
        const bool search = [&] { for (const auto& r : SettingsUI::SearchSettings(L"importer", false, import)) if (r.entry->hit == SettingsUI::ImportData) return true; return false; }();
        return std::make_tuple(Hit(slots, SettingsUI::ManagePasswords) != nullptr, Hit(slots, SettingsUI::ImportData) != nullptr, search,
                               Hit(slots, SettingsUI::ManagePasswords) ? Hit(slots, SettingsUI::ManagePasswords)->value : std::wstring());
    };
    {
        auto [pw, imp, found, count] = privacy(true, true);
        Check(pw && count == L"3", "active vault (synthetic): « Gérer les mots de passe » shown with its count");
        Check(imp && found, "import row shown and found by the search");
    }
    { auto [pw, imp, found, count] = privacy(false, true); Check(!pw && imp, "no vault info: no password row, import row still there"); (void)found; (void)count; }
    { auto [pw, imp, found, count] = privacy(true, false); Check(pw && !imp && !found, "private window (no import hook): no import row, not in the search"); (void)count; }
    std::printf("%d checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    ExitProcess(failures ? 1 : 0);   // windows still owned by live drawers: no teardown ordering to get wrong
}
