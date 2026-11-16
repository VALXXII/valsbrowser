// One taskbar identity for every installed version (src/taskbar-identity.h). Temporary folders and an
// off-screen test window only; never the person's Start Menu or taskbar. -Wall -Wextra -Werror (run_all.py).
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include "../src/taskbar-identity.h"

namespace fs = std::filesystem;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); }

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::printf("FAIL COM\n"); return 1; }
    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
    const fs::path work = fs::path(temp) / (L"taskbar-identity-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec; fs::remove_all(work, ec); fs::create_directories(work / L"browser" / L"versions" / L"0.3.376");
    const std::wstring root = (work / L"browser").wstring();

    // Installed: <root>\versions\<v>\browser.exe. Every version shares the identity; relaunch = the stable launcher.
    const auto a = TaskbarIdentity::ForInstalled(root + L"\\versions\\0.3.376\\browser.exe", true);
    const auto b = TaskbarIdentity::ForInstalled(root + L"\\versions\\0.3.377\\browser.exe", true);
    Check(a.active() && a.aumid == TaskbarIdentity::AppId && a.aumid == b.aumid, "installed: one identity, the same for every version");
    Check(a.relaunch == L"\"" + root + L"\\browser.exe\"" && a.icon == root + L"\\browser.ico,0" && a.name == L"browser", "relaunch: the stable launcher <root>\\browser.exe, its icon, « browser »");
    Check(a.launcher == root + L"\\browser.exe", "the launcher path");
    Check(!TaskbarIdentity::ForInstalled(root + L"\\versions\\0.3.376\\browser.exe", false).active(), "portable or dev copy: no identity (Windows keeps grouping by its own path)");

    // A window carries the identity and its relaunch command (what pinning it records).
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TaskbarIdentityTest"; RegisterClassW(&wc);
    HWND w = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"t", WS_POPUP, -3000, -3000, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
    Check(TaskbarIdentity::ApplyToWindow(w, a), "applied to a window");
    Check(TaskbarIdentity::WindowValue(w, PKEY_AppUserModel_ID) == a.aumid, "the window reads back the identity");
    Check(TaskbarIdentity::WindowValue(w, PKEY_AppUserModel_RelaunchCommand) == a.relaunch, "the window reads back the relaunch command (the launcher)");
    Check(TaskbarIdentity::WindowValue(w, PKEY_AppUserModel_RelaunchDisplayNameResource) == L"browser" &&
          TaskbarIdentity::WindowValue(w, PKEY_AppUserModel_RelaunchIconResource) == a.icon, "with its display name and icon");
    DestroyWindow(w);

    // Shortcuts to the launcher carry the identity (so a pin from the Start Menu groups with the windows).
    const std::wstring ours = (work / L"browser.lnk").wstring(), foreign = (work / L"other.lnk").wstring();
    Check(TaskbarIdentity::WriteShortcut(ours, a.launcher) && TaskbarIdentity::WriteShortcut(foreign, root + L"\\versions\\0.3.376\\browser.exe"), "test shortcuts written");
    Check(TaskbarIdentity::ShortcutId(ours).empty(), "a shortcut from an earlier setup has no identity");
    Check(TaskbarIdentity::StampShortcut(ours, a) == TaskbarIdentity::Stamp::Stamped && TaskbarIdentity::ShortcutId(ours) == a.aumid, "our launcher shortcut is stamped once");
    Check(TaskbarIdentity::StampShortcut(ours, a) == TaskbarIdentity::Stamp::AlreadyStamped, "then left alone (no rewrite at every start)");
    Check(TaskbarIdentity::StampShortcut(foreign, a) == TaskbarIdentity::Stamp::NotOurs && TaskbarIdentity::ShortcutId(foreign).empty(), "a shortcut to anything else is never touched");
    Check(TaskbarIdentity::StampShortcut((work / L"missing.lnk").wstring(), a) == TaskbarIdentity::Stamp::Missing, "no shortcut: nothing created");

    // The path the browser stamps at start (main.cpp StampTaskbarIdentity builds it with LauncherShortcut): a real
    // separator, no control character (4e37034 had « \b » = backspace there, so it never found the shortcut).
    {
        const std::wstring folder = (work / L"Start Menu").wstring();
        fs::create_directories(folder, ec);
        const std::wstring lnk = TaskbarIdentity::LauncherShortcut(folder);
        bool control = false; for (wchar_t c : lnk) if (c < 0x20) control = true;
        Check(lnk == folder + L"\\browser.lnk" && !control && fs::path(lnk).filename() == L"browser.lnk" && fs::path(lnk).parent_path() == fs::path(folder), "launcher shortcut path: folder + separator + browser.lnk, no control character");
        Check(TaskbarIdentity::WriteShortcut(lnk, a.launcher) && TaskbarIdentity::StampShortcut(lnk, a) == TaskbarIdentity::Stamp::Stamped && TaskbarIdentity::ShortcutId(lnk) == a.aumid, "the shortcut at that path (synthetic folder) is found and stamped");
    }

    fs::remove_all(work, ec);
    CoUninitialize();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
