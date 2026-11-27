// What a browser stopped in the middle of « Conserver » leaves in <dataDir>\kept-downloads, at the next start
// (src/kept-downloads.h Sweep, AdoptedLook). Windowless: temporary folders under the output folder, no browser, no engine.
//   kept_downloads_test.exe <output folder>
// A leftover is never dropped on the spot (night run 2026-11-26: the restarted engine removed the file ~8 s after the
// kill, after a sweep had judged it in place): it is claimed by this process (an atomic rename), given back where the
// file is missing, and watched until the guard counted from the engine's start is over. A live window's keep is untouched;
// the path taken by another file -> the next free name; no readable sidecar -> the download folder under its own name.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <cstdio>
#include <string>
#include "../../src/kept-downloads.h"

static int failures = 0;
static void Expect(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static void Put(const std::wstring& p, const std::string& bytes) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); DWORD n = 0;
    WriteFile(h, bytes.data(), (DWORD)bytes.size(), &n, nullptr); CloseHandle(h);
}
static std::string Get(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "<none>";
    char buf[256]; DWORD n = 0; ReadFile(h, buf, sizeof buf, &n, nullptr); CloseHandle(h); return std::string(buf, n);
}
static void Wipe(const std::wstring& dir) {
    WIN32_FIND_DATAW f{}; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &f);
    if (h != INVALID_HANDLE_VALUE) { do { const std::wstring n = f.cFileName; if (n != L"." && n != L"..") DeleteFileW((dir + L"\\" + n).c_str()); } while (FindNextFileW(h, &f)); FindClose(h); }
    RemoveDirectoryW(dir.c_str());
}
static const KeptDownloads::Result* Find(const std::vector<KeptDownloads::Result>& rs, const std::wstring& leftover) {
    for (const auto& r : rs) if (r.leftover == leftover) return &r;
    return nullptr;
}

int wmain(int argc, wchar_t** argv) {
    using namespace KeptDownloads;
    if (argc < 2) { std::printf("usage: kept_downloads_test.exe <output folder>\n"); return 2; }
    const std::wstring root = std::wstring(argv[1]) + L"\\kept-downloads-test", kept = root + L"\\kept-downloads", dl = root + L"\\downloads";
    Wipe(kept); Wipe(dl); RemoveDirectoryW(root.c_str());
    CreateDirectoryW(root.c_str(), nullptr); CreateDirectoryW(kept.c_str(), nullptr); CreateDirectoryW(dl.c_str(), nullptr);
    const unsigned long dead = 4000001, alive = 4000002, own = 4000100, other = 4000200;
    unsigned long pid = 0; std::wstring file;
    Expect(ParseName(L"123-4-setup.exe", pid, file) && pid == 123 && file == L"setup.exe", "names: <pid>-<id>-<file>");
    Expect(!ParseName(L"setup.exe", pid, file) && !ParseName(L"123-setup.exe", pid, file) && !ParseName(L"123-4-setup.exe.path", pid, file), "names: anything else, or a sidecar, is not a leftover");

    // The watch's decisions (pure).
    Expect(AdoptedLook(false, 0, true) == Watch::Wait && AdoptedLook(false, 999999, true) == Watch::Wait, "before this profile's engine is up: never settled, however long (its clean-up comes after its start)");
    Expect(AdoptedLook(true, AdoptedGuardMs - 1, true) == Watch::Wait, "the engine up, the guard not over: the file in place is NOT taken as safe");
    Expect(AdoptedLook(false, 0, false) == Watch::GiveBack && AdoptedLook(true, 5000, false) == Watch::GiveBack && AdoptedLook(true, AdoptedGuardMs + 1, false) == Watch::GiveBack, "a removal, before or during the guard: given back from our name");
    Expect(AdoptedLook(true, AdoptedGuardMs, true) == Watch::Settle && AdoptedGuardMs >= 60000, "after the guard counted from the engine's start (or the last give-back), with the file there: settled");

    // 1. in place: the file is at its recorded path and is our hard link -> claimed and watched, NOT cleaned.
    const std::wstring inPlace = dl + L"\\in-place.exe", b1 = kept + L"\\" + std::to_wstring(dead) + L"-1-in-place.exe";
    Put(inPlace, "in place bytes"); CreateHardLinkW(b1.c_str(), inPlace.c_str(), nullptr); WriteSidecar(b1, inPlace);
    // 2. missing: the engine removed the file and the browser was stopped before giving it back -> given back, watched.
    const std::wstring missing = dl + L"\\missing.exe", b2 = kept + L"\\" + std::to_wstring(dead) + L"-2-missing.exe";
    Put(b2, "missing bytes"); WriteSidecar(b2, missing);
    // 3. a live window's keep: untouched, whatever its state.
    const std::wstring b3 = kept + L"\\" + std::to_wstring(alive) + L"-3-live.exe";
    Put(b3, "live bytes"); WriteSidecar(b3, dl + L"\\live.exe");
    // 4. the path taken by another file (a later download with the same name) -> the next free name, nothing to watch.
    const std::wstring taken = dl + L"\\taken.exe", b4 = kept + L"\\" + std::to_wstring(dead) + L"-4-taken.exe";
    Put(taken, "someone else's, longer bytes"); Put(b4, "taken bytes"); WriteSidecar(b4, taken);
    // 5. no readable sidecar -> the download folder, under the file's own name.
    const std::wstring b5 = kept + L"\\" + std::to_wstring(dead) + L"-5-lost note.exe";
    Put(b5, "no note bytes");
    // 6. a sidecar left alone (its file already went) of a dead process -> dropped.
    const std::wstring b6 = kept + L"\\" + std::to_wstring(dead) + L"-6-gone.exe"; WriteSidecar(b6, dl + L"\\gone.exe");
    // 7. the engine left an empty file at the path (a cancel during its pass) -> replaced by the kept bytes, watched.
    const std::wstring stub = dl + L"\\stub.exe", b7 = kept + L"\\" + std::to_wstring(dead) + L"-7-stub.exe";
    Put(stub, ""); Put(b7, "stub bytes"); WriteSidecar(b7, stub);

    int lines = 0;
    const auto results = Sweep(kept, dl, [&](unsigned long p) { return p == alive; }, [&](const std::wstring&) { ++lines; }, own);

    const auto* r1 = Find(results, b1);
    Expect(r1 && r1->outcome == Outcome::Adopted && Get(inPlace) == "in place bytes" && !Exists(b1) && Exists(r1->claimed) && Exists(SidecarOf(r1->claimed)) && SameFileId(inPlace, r1->claimed),
           "in place: claimed by this process (renamed), its note with it, the file untouched, NOT cleaned");
    Expect(r1 && r1->claimed.find(L"\\" + std::to_wstring(own) + L"-") != std::wstring::npos, "the claimed name carries this process's pid (another process now leaves it alone)");
    const auto* r2 = Find(results, b2);
    Expect(r2 && r2->outcome == Outcome::Restored && Get(missing) == "missing bytes" && Exists(r2->claimed) && Exists(SidecarOf(r2->claimed)), "missing: the bytes given back at the recorded path, our name kept to watch it");
    Expect(Get(b3) == "live bytes" && Exists(SidecarOf(b3)) && !Exists(dl + L"\\live.exe"), "live window: its keep is untouched");
    const auto* r4 = Find(results, b4);
    Expect(r4 && r4->outcome == Outcome::RestoredElsewhere && Get(taken) == "someone else's, longer bytes" && Get(dl + L"\\taken (1).exe") == "taken bytes" && !Exists(r4->claimed) && !Exists(SidecarOf(r4->claimed)),
           "path taken: the other file untouched, ours under the next free name (a name the engine never knew), nothing of ours left");
    Expect(Get(dl + L"\\lost note.exe") == "no note bytes" && !Exists(b5), "no note: the download folder under the file's own name");
    Expect(!Exists(SidecarOf(b6)), "a note alone of a dead process is dropped");
    const auto* r7 = Find(results, b7);
    Expect(r7 && r7->outcome == Outcome::Restored && Get(stub) == "stub bytes" && Exists(r7->claimed) && !Exists(dl + L"\\stub (1).exe"), "an empty leftover of the engine at the path: replaced by the kept bytes, watched");
    Expect(lines == 5, "one log line per leftover changed (the live one untouched, unlogged)");

    // A second process of the same start (a window restoring another window's session): the claimed names belong to a
    // live process now, it leaves them alone; the live window's keep too.
    int again = 0;
    const auto second = Sweep(kept, dl, [&](unsigned long p) { return p == alive || p == own; }, [&](const std::wstring&) { ++again; }, other);
    bool untouched = again == 0;
    for (const auto& r : second) untouched = untouched && r.outcome == Outcome::Untouched;
    Expect(untouched && r1 && Exists(r1->claimed) && Get(inPlace) == "in place bytes", "a second process sweeping at the same start takes nothing: no second log line, nothing moved");

    // The first process stopped too (its pid gone): the next start claims the leftover again; the claim is a rename, so of
    // two processes racing for it only one wins (the other one's rename fails: TakenElsewhere).
    if (r1) {
        const std::wstring claimedByOwn = r1->claimed;
        const auto third = Sweep(kept, dl, [&](unsigned long p) { return p == alive; }, [](const std::wstring&) {}, other, 900000500);
        const auto* r = Find(third, claimedByOwn);
        Expect(r && r->outcome == Outcome::Adopted && !Exists(claimedByOwn) && Exists(r->claimed), "a next start claims it again (still watched, still not cleaned)");
        Expect(!MoveFileExW(claimedByOwn.c_str(), (kept + L"\\x.exe").c_str(), 0), "the claim is atomic: the old name no longer exists for a second claimer");
    }
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "kept downloads checks passed", failures);
    return failures ? 1 : 0;
}
