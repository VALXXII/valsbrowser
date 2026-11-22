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
    Check(a.relaunch == L"\"" + root + L"\\browser.exe\"" && a.icon == root + L"\\browser.ico,0" && a.name == L"@" + root + L"\\browser.exe,-1", "relaunch: the stable launcher <root>\\browser.exe, its icon, its name « Browser » (the launcher's string 1)");
    Check(a.aumid == L"ValXXII.browser", "the AppUserModelID itself never changes (pins and grouping keep working)");
    Check(a.launcher == root + L"\\browser.exe", "the launcher path");
    Check(!TaskbarIdentity::ForInstalled(root + L"\\versions\\0.3.376\\browser.exe", false).active(), "portable or dev copy: no identity (Windows keeps grouping by its own path)");

    // A window carries the identity and its relaunch command (what pinning it records).
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TaskbarIdentityTest"; RegisterClassW(&wc);
    HWND w = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"t", WS_POPUP, -3000, -3000, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
    Check(TaskbarIdentity::ApplyToWindow(w, a), "applied to a window");
    Check(TaskbarIdentity::WindowValue(w, PKEY_AppUserModel_ID) == a.aumid, "the window reads back the identity");
    Check(TaskbarIdentity::WindowValue(w, PKEY_AppUserModel_RelaunchCommand) == a.relaunch, "the window reads back the relaunch command (the launcher)");
    // (A plain « Browser » would read back « browser »: window properties are case-insensitive atoms.)
    Check(TaskbarIdentity::WindowValue(w, PKEY_AppUserModel_RelaunchDisplayNameResource) == a.name &&
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
        Check(lnk == folder + L"\\Browser.lnk" && !control && fs::path(lnk).filename() == L"Browser.lnk" && fs::path(lnk).parent_path() == fs::path(folder), "launcher shortcut path: folder + separator + Browser.lnk, no control character");
        Check(TaskbarIdentity::WriteShortcut(lnk, a.launcher) && TaskbarIdentity::StampShortcut(lnk, a) == TaskbarIdentity::Stamp::Stamped && TaskbarIdentity::ShortcutId(lnk) == a.aumid, "the shortcut at that path (synthetic folder) is found and stamped");
    }

    // « browser » -> « Browser »: an existing shortcut of ours (from an earlier setup) is renamed IN PLACE (case only,
    // same file): its target and identity stay, so what was pinned from it keeps working. Never anyone else's link.
    {
        const fs::path folder = work / L"Programs";
        fs::create_directories(folder, ec);
        const std::wstring legacy = (folder / L"browser.lnk").wstring();
        Check(TaskbarIdentity::WriteShortcut(legacy, a.launcher, &a), "an earlier setup's « browser.lnk » (with identity)");
        const auto created = fs::last_write_time(legacy, ec);
        Check(TaskbarIdentity::OnDiskName(legacy) == L"browser.lnk", "on disk as « browser.lnk »");
        Check(TaskbarIdentity::RenameShortcut(folder.wstring(), a.launcher) == TaskbarIdentity::Rename::Renamed, "renamed");
        Check(TaskbarIdentity::OnDiskName(TaskbarIdentity::LauncherShortcut(folder.wstring())) == L"Browser.lnk", "now « Browser.lnk » on disk (what the Start Menu shows)");
        Check(TaskbarIdentity::ShortcutId(TaskbarIdentity::LauncherShortcut(folder.wstring())) == a.aumid &&
              TaskbarIdentity::Link(TaskbarIdentity::LauncherShortcut(folder.wstring()), true).Target() == a.launcher &&
              fs::last_write_time(TaskbarIdentity::LauncherShortcut(folder.wstring()), ec) == created, "same file: target, identity and contents untouched");
        Check(TaskbarIdentity::RenameShortcut(folder.wstring(), a.launcher) == TaskbarIdentity::Rename::AlreadyNamed, "then left alone");
        // Another copy's « browser » shortcut keeps its name.
        const fs::path otherFolder = work / L"Other"; fs::create_directories(otherFolder, ec);
        const std::wstring foreignLegacy = (otherFolder / L"browser.lnk").wstring();
        TaskbarIdentity::WriteShortcut(foreignLegacy, root + L"\\versions\\0.3.376\\browser.exe");
        Check(TaskbarIdentity::RenameShortcut(otherFolder.wstring(), a.launcher) == TaskbarIdentity::Rename::NotOurs && TaskbarIdentity::OnDiskName(foreignLegacy) == L"browser.lnk", "a shortcut to anything else keeps its name");
        Check(TaskbarIdentity::RenameShortcut((work / L"Empty").wstring(), a.launcher) == TaskbarIdentity::Rename::Missing, "no shortcut: nothing created");
    }

    fs::remove_all(work, ec);
    CoUninitialize();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
