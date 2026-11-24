// Directory loops that never throw (src/fs-safe.h, audit SEC-27). Temporary folders only.
// Compiled with -Wall -Wextra -Werror (tests/update-integration/run_all.py).
#include <windows.h>
#include <sddl.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include "../src/fs-safe.h"

namespace fs = std::filesystem;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); }
static bool Acl(const fs::path& p, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
    const bool ok = SetFileSecurityW(p.c_str(), DACL_SECURITY_INFORMATION, sd) != 0;
    LocalFree(sd);
    return ok;
}

int main() {
    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
    const fs::path work = fs::path(temp) / (L"fs-safe-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(work); fs::create_directories(work / L"a" / L"locked"); fs::create_directories(work / L"b");
    std::ofstream(work / L"a" / L"locked" / L"inside.txt") << "x";
    std::ofstream(work / L"top.txt") << "x";

    std::error_code ec;
    std::set<std::wstring> mine, theirs;
    for (const auto& e : FsSafe::Entries(work, ec)) mine.insert(e.path().filename().wstring());
    for (const auto& e : fs::directory_iterator(work)) theirs.insert(e.path().filename().wstring());
    Check(!ec && mine == theirs && mine.size() == 3, "same entries as directory_iterator");
    int n = 0;
    for (const auto& e : FsSafe::Entries(work / L"missing", ec)) { (void)e; ++n; }
    Check(n == 0 && ec, "a missing folder: no entry, the error is in ec");

    // A sub-folder that cannot be listed: the standard loop's ++ throws, FsSafe ends the loop with the error.
    const bool locked = Acl(work / L"a" / L"locked", L"D:(D;;0x1;;;WD)(A;OICI;FA;;;WD)");
    Check(locked, "test folder made unlistable");
    bool threw = false;
    try { std::error_code c; for (const auto& e : fs::recursive_directory_iterator(work, c)) (void)e; } catch (const fs::filesystem_error&) { threw = true; }
    Check(threw, "precondition: recursive_directory_iterator(p, ec) still throws from ++");
    bool safeThrew = false; n = 0;
    try { for (const auto& e : FsSafe::EntriesRecursive(work, ec)) { (void)e; ++n; } } catch (...) { safeThrew = true; }
    Check(!safeThrew && ec, "FsSafe::EntriesRecursive: no exception, the error is in ec");

    Acl(work / L"a" / L"locked", L"D:(A;OICI;FA;;;WD)");
    fs::remove_all(work, ec);
    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
