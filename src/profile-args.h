#pragma once
// Command-line helpers of the profile integration (pure, unit-tested in tests/ui/profile_ui_test.cpp).
// `--delete-profile <id>` is ONE-SHOT: consumed by the window that takes a delete over, it must never
// travel into a relaunch (engine recovery, orphan relaunch), or a later recovery would delete the
// profile again after an undo (re-review R2). `--profile <id>` is kept.
#include <string>

namespace ProfileArgs {
// The value after `name ` (`--profile travail` -> `travail`), empty when absent.
inline std::wstring Value(const std::wstring& args, const std::wstring& name) {
    const auto at = args.find(name + L" "); if (at == std::wstring::npos) return L"";
    size_t b = at + name.size() + 1; while (b < args.size() && args[b] == L' ') ++b;
    if (b < args.size() && args[b] == L'-') return L"";               // value-less: the next token is a flag
    size_t e = b; while (e < args.size() && args[e] != L' ') ++e;
    return args.substr(b, e - b);
}
// `args` without every `name <value>` occurrence; the other arguments keep their order and spacing.
inline std::wstring Strip(std::wstring args, const std::wstring& name) {
    for (;;) {
        const auto at = args.find(name); if (at == std::wstring::npos) break;
        // The flag, plus its value only when one follows (a token not starting with '-'): a value-less
        // `--delete-profile --profile x` never swallows the next flag.
        size_t e = at + name.size(); size_t v = e; while (v < args.size() && args[v] == L' ') ++v;
        if (v < args.size() && args[v] != L'-') { e = v; while (e < args.size() && args[e] != L' ') ++e; }
        size_t b = at; while (b > 0 && args[b - 1] == L' ') --b;
        args.erase(b, e - b);
    }
    while (!args.empty() && args.front() == L' ') args.erase(args.begin());
    return args;
}
// Arguments safe to hand to a relaunch of this window.
inline std::wstring ForRelaunch(const std::wstring& args) { return Strip(args, L"--delete-profile"); }
}

// Delete take-over reservation of one window (pure; the host keeps one). Accepting a take-over reserves it
// synchronously, before the WM_COPYDATA answer leaves, so nothing can start a local undo in between: the
// requester closes its windows only for a delete this window is bound to perform. Cleared when that delete
// ends (done, failed, timed out). No lock is held across processes.
namespace ProfileTakeOver {
struct Gate {
    std::wstring reserved;
    // undoPending: this window already shows an undo; blocked: private window / profiles off / own profile / bad id.
    bool Reserve(const std::wstring& id, bool undoPending, bool blocked) { if (undoPending || blocked || id.empty() || !reserved.empty()) return false; reserved = id; return true; }
    bool Busy() const { return !reserved.empty(); }                      // refuse any other local delete meanwhile
    bool Owns(const std::wstring& id) const { return !reserved.empty() && reserved == id; }
    void Clear() { reserved.clear(); }
};
}
