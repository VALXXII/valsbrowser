// The ad blocker installs from a folder that does not move with each update (src/adblock-folder.h).
// Temporary folders only. Compiled with -Wall -Wextra -Werror (run_all.py).
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../src/adblock-folder.h"

namespace fs = std::filesystem;
static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); }
static void Write(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s; }
static std::string Read(const fs::path& p) { std::ifstream in(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), {}); }
static void Bundle(const fs::path& dir, const std::string& version, const std::string& rules) {
    Write(dir / "manifest.json", "{\n  \"name\": \"uBlock Origin Lite\",\n  \"version\": \"" + version + "\",\n  \"manifest_version\": 3\n}\n");
    Write(dir / "rulesets" / "main" / "default.json", rules);
}

int main() {
    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
    const fs::path work = fs::path(temp) / (L"adblock-folder-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(work); fs::create_directories(work);
    const fs::path root = work / L"data";
    const fs::path v1 = work / L"versions" / L"0.3.1" / L"assets" / L"extensions" / L"ublock-lite";
    const fs::path v2 = work / L"versions" / L"0.3.2" / L"assets" / L"extensions" / L"ublock-lite";
    Bundle(v1, "2026.1103.1743", "rules-a"); Bundle(v2, "2026.1103.1743", "rules-a");

    Check(AdBlockFolder::ManifestVersion(v1.wstring()) == L"2026.1103.1743", "the bundled manifest's version is read");
    std::wstring note;
    const std::wstring expected = AdBlockFolder::Expected(v1.wstring(), root.wstring());
    Check(!fs::exists(root / L"extensions"), "Expected() copies nothing");
    const std::wstring first = AdBlockFolder::Stable(v1.wstring(), root.wstring(), &note);
    Check(expected == first, "Expected() names the very folder Stable() installs from (the browser compares it with its last install)");
    const fs::path expect = root / L"extensions" / L"ublock-lite-2026.1103.1743";
    Check(fs::path(first) == expect, "installs from <data>\\extensions\\ublock-lite-<version>, not from the version folder");
    Check(Read(expect / "rulesets" / "main" / "default.json") == "rules-a" && fs::exists(expect / "manifest.json"), "the whole extension is copied there");
    int entries = 0; for (auto& e : fs::directory_iterator(root / L"extensions")) { (void)e; ++entries; }
    Check(entries == 1 && note.find(L"copied to") == 0, "copied once, no temporary folder left, the reason is logged");

    // The update: same extension version, bundled under another version folder -> the same stable folder, no copy.
    const auto stamp = fs::last_write_time(expect / "manifest.json");
    Sleep(20);
    const std::wstring second = AdBlockFolder::Stable(v2.wstring(), root.wstring(), &note);
    Check(second == first, "after an update with the same uBlock version: the same folder (the extension id does not change)");
    Check(fs::last_write_time(expect / "manifest.json") == stamp, "nothing copied again");

    // A new uBlock version: a new folder, the previous one left for profiles that have not synced yet.
    const fs::path v3 = work / L"versions" / L"0.3.3" / L"assets" / L"extensions" / L"ublock-lite";
    Bundle(v3, "2026.1201.0900", "rules-b");
    const std::wstring third = AdBlockFolder::Stable(v3.wstring(), root.wstring(), &note);
    Check(fs::path(third) == root / L"extensions" / L"ublock-lite-2026.1201.0900", "a new uBlock version gets its own folder");
    Check(AdBlockFolder::Expected(v3.wstring(), root.wstring()) == third && third != first, "Expected() follows the version: another folder than the last install's");
    Check(Read(fs::path(third) / "rulesets" / "main" / "default.json") == "rules-b" && fs::exists(expect), "with its own rules; the previous copy is kept");

    // A copy cut short (the browser closed while its background copy ran) left its temporary folder: the next copy
    // removes it once its process is gone; another window's copy in progress (its process runs) is left alone.
    const fs::path extensions = root / L"extensions";
    const fs::path dead = extensions / L"ublock-lite-2026.1201.0900.tmp-4294967292";   // no process has that id
    const fs::path running = extensions / L"ublock-lite-2026.1201.0900.tmp-4";          // the System process: always there
    const fs::path other = extensions / L"notes.tmp-4294967292";                       // not ours: never touched
    Write(dead / "rulesets" / "main" / "default.json", "half"); Write(running / "manifest.json", "{}"); Write(other / "x", "x");
    const fs::path v5 = work / L"bundle5";
    Bundle(v5, "2026.1215.1000", "rules-d");
    const std::wstring fifth = AdBlockFolder::Stable(v5.wstring(), root.wstring(), &note);
    Check(fs::path(fifth) == extensions / L"ublock-lite-2026.1215.1000", "a new version is copied as before");
    Check(!fs::exists(dead) && note.find(L"1 unfinished copy removed") != std::wstring::npos, "an unfinished copy of a process that is gone is removed (and logged)");
    Check(fs::exists(running) && fs::exists(other), "a copy whose process still runs, and a folder that is not a copy, are left alone");
    Check(AdBlockFolder::Stable(v5.wstring(), root.wstring(), &note) == fifth && note.find(L"kept") == 0, "the next launch keeps the folder: nothing to clean, nothing copied");
    fs::remove_all(running); fs::remove_all(other);

    // A folder left half-written by a crash (no manifest) is never used.
    const fs::path broken = root / L"extensions" / L"ublock-lite-2027.1.1";
    fs::create_directories(broken / "rulesets");
    const fs::path v4 = work / L"bundle4";
    Bundle(v4, "2027.1.1", "rules-c");
    Check(AdBlockFolder::Stable(v4.wstring(), root.wstring(), &note) == v4.wstring(), "a stable folder without its manifest is not used: the bundled one is");

    // No version, a version with unexpected characters, no data root: the bundled folder, as before.
    const fs::path odd = work / L"odd"; Write(odd / "manifest.json", "{ \"version\": \"1.0/../..\" }");
    Check(AdBlockFolder::Stable(odd.wstring(), root.wstring(), &note) == odd.wstring(), "a version that is not digits and dots: the bundled folder");
    const fs::path none = work / L"none"; Write(none / "manifest.json", "{ \"name\": \"x\" }");
    Check(AdBlockFolder::Stable(none.wstring(), root.wstring(), &note) == none.wstring(), "no version: the bundled folder");
    Check(AdBlockFolder::Stable(v1.wstring(), L"", &note) == v1.wstring(), "no data root: the bundled folder");
    Check(AdBlockFolder::Expected(v1.wstring(), L"") == v1.wstring() && AdBlockFolder::Expected(odd.wstring(), root.wstring()) == odd.wstring()
          && AdBlockFolder::Expected(none.wstring(), root.wstring()) == none.wstring(), "Expected(): the bundled folder in those cases too");
    Check(AdBlockFolder::Stable((work / L"missing").wstring(), root.wstring(), &note) == (work / L"missing").wstring(), "no bundled extension: returned unchanged (the caller reports it)");

    std::error_code ec; fs::remove_all(work, ec);
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
