// What a browser killed in the middle of « Conserver » leaves in <dataDir>\kept-downloads, given back at the next start
// (src/kept-downloads.h Sweep). Windowless: temporary folders under the output folder, no browser, no engine.
//   kept_downloads_test.exe <output folder>
// Cases: in place (the file at its recorded path is ours) -> our name cleaned; missing -> the bytes restored at the
// recorded path; a live window's keep -> untouched; the path taken by another file -> the next free name; no readable
// sidecar -> the download folder under the file's own name. Nothing is ever deleted before the bytes are visible.
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

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("usage: kept_downloads_test.exe <output folder>\n"); return 2; }
    const std::wstring root = std::wstring(argv[1]) + L"\\kept-downloads-test", kept = root + L"\\kept-downloads", dl = root + L"\\downloads";
    Wipe(kept); Wipe(dl); RemoveDirectoryW(root.c_str());
    CreateDirectoryW(root.c_str(), nullptr); CreateDirectoryW(kept.c_str(), nullptr); CreateDirectoryW(dl.c_str(), nullptr);
    const unsigned long dead = 4000001, alive = 4000002;
    unsigned long pid = 0; std::wstring file;
    Expect(KeptDownloads::ParseName(L"123-4-setup.exe", pid, file) && pid == 123 && file == L"setup.exe", "names: <pid>-<id>-<file>");
    Expect(!KeptDownloads::ParseName(L"setup.exe", pid, file) && !KeptDownloads::ParseName(L"123-setup.exe", pid, file) && !KeptDownloads::ParseName(L"123-4-setup.exe.path", pid, file), "names: anything else, or a sidecar, is not a leftover");

    // 1. in place: the file is at its recorded path and is our hard link -> our name and its sidecar go, the file stays.
    const std::wstring inPlace = dl + L"\\in-place.exe", b1 = kept + L"\\" + std::to_wstring(dead) + L"-1-in-place.exe";
    Put(inPlace, "in place bytes"); CreateHardLinkW(b1.c_str(), inPlace.c_str(), nullptr); KeptDownloads::WriteSidecar(b1, inPlace);
    // 2. missing: the engine removed the file and the browser was killed before giving it back -> restored at its path.
    const std::wstring missing = dl + L"\\missing.exe", b2 = kept + L"\\" + std::to_wstring(dead) + L"-2-missing.exe";
    Put(b2, "missing bytes"); KeptDownloads::WriteSidecar(b2, missing);
    // 3. a live window's keep: untouched, whatever its state.
    const std::wstring b3 = kept + L"\\" + std::to_wstring(alive) + L"-3-live.exe";
    Put(b3, "live bytes"); KeptDownloads::WriteSidecar(b3, dl + L"\\live.exe");
    // 4. the path taken by another file (a later download with the same name) -> the next free name.
    const std::wstring taken = dl + L"\\taken.exe", b4 = kept + L"\\" + std::to_wstring(dead) + L"-4-taken.exe";
    Put(taken, "someone else's, longer bytes"); Put(b4, "taken bytes"); KeptDownloads::WriteSidecar(b4, taken);
    // 5. no readable sidecar -> the download folder, under the file's own name.
    const std::wstring b5 = kept + L"\\" + std::to_wstring(dead) + L"-5-lost note.exe";
    Put(b5, "no note bytes");
    // 7. the engine left an empty file at the path (a cancel during its pass) -> replaced by the kept bytes.
    const std::wstring stub = dl + L"\\stub.exe", b7 = kept + L"\\" + std::to_wstring(dead) + L"-7-stub.exe";
    Put(stub, ""); Put(b7, "stub bytes"); KeptDownloads::WriteSidecar(b7, stub);
    // 6. a sidecar left alone (its file already went) of a dead process -> dropped.
    const std::wstring b6 = kept + L"\\" + std::to_wstring(dead) + L"-6-gone.exe"; KeptDownloads::WriteSidecar(b6, dl + L"\\gone.exe");

    int lines = 0;
    const auto results = KeptDownloads::Sweep(kept, dl, [&](unsigned long p) { return p == alive; }, [&](const std::wstring&) { ++lines; });

    Expect(Get(inPlace) == "in place bytes" && !KeptDownloads::Exists(b1) && !KeptDownloads::Exists(KeptDownloads::SidecarOf(b1)), "in place: the file stays, our name and its note are cleaned");
    Expect(Get(missing) == "missing bytes" && !KeptDownloads::Exists(b2) && !KeptDownloads::Exists(KeptDownloads::SidecarOf(b2)), "missing: the bytes are back at the recorded path, nothing of ours left");
    Expect(Get(b3) == "live bytes" && KeptDownloads::Exists(KeptDownloads::SidecarOf(b3)) && !KeptDownloads::Exists(dl + L"\\live.exe"), "live window: its keep is untouched");
    Expect(Get(taken) == "someone else's, longer bytes" && Get(dl + L"\\taken (1).exe") == "taken bytes" && !KeptDownloads::Exists(b4), "path taken: the other file untouched, ours under the next free name");
    Expect(Get(dl + L"\\lost note.exe") == "no note bytes" && !KeptDownloads::Exists(b5), "no note: the download folder under the file's own name");
    Expect(!KeptDownloads::Exists(KeptDownloads::SidecarOf(b6)), "a note alone of a dead process is dropped");
    Expect(Get(stub) == "stub bytes" && !KeptDownloads::Exists(b7) && !KeptDownloads::Exists(dl + L"\\stub (1).exe"), "an empty leftover of the engine at the path: replaced by the kept bytes");
    Expect(results.size() == 6 && lines == 5, "one result per leftover (the live one untouched, unlogged), one log line per change");
    // Run again: nothing more to do, nothing touched.
    int again = 0; KeptDownloads::Sweep(kept, dl, [&](unsigned long p) { return p == alive; }, [&](const std::wstring&) { ++again; });
    Expect(again == 0 && Get(b3) == "live bytes", "a second start: nothing more to do");
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "kept downloads checks passed", failures);
    return failures ? 1 : 0;
}
