// #121 night QA helper (tests/e2e/autofill_qa.ps1): writes a synthetic profile's vault and typed-info files through the
// production stores (Vault::Commit, Forms::Commit), so the merged list has a FICTITIOUS account and e-mails to show.
// Never shipped, never run on a real profile (the QA script passes its own build\ folder).
// Usage: autofill_seed.exe <profile data dir> <https origin> <username|-> <password|-> [e-mail ...]   (« - »: no account)
#include "../../src/form-memory.h"
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: autofill_seed <dir> <origin> <user> <password> [e-mail ...]\n"); return 2; }
    const std::wstring dir = argv[1], origin = argv[2], user = argv[3], secret = argv[4];
    if (dir.find(L"\\build\\") == std::wstring::npos) { std::fprintf(stderr, "refused: the profile must be under a build\\ folder\n"); return 3; }
    const std::int64_t now = static_cast<std::int64_t>(GetTickCount64());
    Vault::Store vb, vm; const bool account = user != L"-";
    if (account && vm.Save(origin, user, secret, now) != Vault::Store::Saved::Added) { std::fprintf(stderr, "vault save refused\n"); return 4; }
    const auto v = account ? Vault::Commit(dir, dir + L"\\passwords", vb, vm) : ProfileStore::Result::Written;
    Forms::Store fb, fm; for (int i = 5; i < argc; ++i) fm.Add(argv[i], now - i);
    const auto f = Forms::Commit(dir, dir + L"\\form-entries", fb, fm);
    ProfileStore::WriteAtomic(dir + L"\\form-entries-engine", "done\n");   // no engine import in the fixture
    std::printf("seeded: vault %d, typed info %d (%zu e-mails)\n", static_cast<int>(v), static_cast<int>(f), fm.Entries().size());
    return v == ProfileStore::Result::Written && (argc == 5 || f == ProfileStore::Result::Written) ? 0 : 1;
}
