#pragma once
// Portable -> installed migration of the annotation store (design/public-download-plan-b.md, items 4 + 6).
// Header-only: shared by the update helper (which does not link the store), the store (src/annotations.cpp)
// and the MCP bridge (src/mcp-bridge.cpp).
//
//   <app>\annotations.write-gate   GATE. Opened SHARED by every request that resolves or uses the portable
//                                  default store (bridge: every request; browser: every store mutation), for the
//                                  whole request. Opened EXCLUSIVE by the update helper from the start of the copy
//                                  until the install outcome (ack or rollback). The handle dies with its process:
//                                  a crashed helper never leaves the gate taken. Outside the store: walks never
//                                  see it.
//   <root>\data\annotations-migration   RECORD (durable). « copied » is written with the copied data; the
//                                  helper rewrites it « committed » only after the new version's startup ack.
//                                  Only « committed » lets a portable bridge redirect to <root>\data\annotations.
//                                  « skipped »: an older migration (before this record existed) with an
//                                  installed store already in use: never redirected.
//   <app>\annotations.migrated     MIRROR of the committed record in the source folder: a portable browser
//                                  process still running there refuses to write to the abandoned store.
// Writers that cannot take the gate (helper holds it) answer Busy « migration en cours, réessayez ».
#include <windows.h>

#include <cwctype>
#include <string>

namespace AnnotationsMigration {

constexpr const wchar_t* RecordName = L"annotations-migration";   // in <root>\data
constexpr const wchar_t* GateSuffix = L".write-gate";             // <storeRoot> + suffix
constexpr const wchar_t* MirrorSuffix = L".migrated";             // <storeRoot> + suffix

enum class State { None, Copied, Committed, Skipped };

struct Record {
    State state = State::None;
    std::string from;   // portable folder (UTF-8, as written in migrated-from.txt)
    std::string nonce;  // install that wrote it
};

inline const char* StateName(State s) {
    switch (s) {
    case State::Copied: return "copied";
    case State::Committed: return "committed";
    case State::Skipped: return "skipped";
    default: return "none";
    }
}

inline std::string Format(const Record& r) {
    return std::string("browser-annotations-migration 1\nstate ") + StateName(r.state) + "\nfrom " + r.from + "\nnonce " + r.nonce + "\n";
}

// Strict: exact header, the three keys in order, LF only, known state, non-empty from. Anything else: None.
inline Record Parse(const std::string& text) {
    Record r;
    if (text.size() > 8192 || text.find('\r') != std::string::npos) return r;
    const char* keys[] = {"browser-annotations-migration", "state", "from", "nonce"};
    std::string values[4];
    std::size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) return r;
        const std::string line = text.substr(pos, end - pos);
        const std::string key = keys[i];
        if (line.compare(0, key.size() + 1, key + " ") != 0) return r;
        values[i] = line.substr(key.size() + 1);
        pos = end + 1;
    }
    if (pos != text.size() || values[0] != "1" || values[2].empty()) return r;
    State s = values[1] == "copied" ? State::Copied : values[1] == "committed" ? State::Committed
            : values[1] == "skipped" ? State::Skipped : State::None;
    if (s == State::None) return r;
    r.state = s;
    r.from = values[2];
    r.nonce = values[3];
    return r;
}

// The gate exists only for stores named "annotations" (the default layout); a custom --store keeps none.
inline std::wstring GatePath(const std::wstring& storeRoot) {
    std::wstring r = storeRoot;
    while (!r.empty() && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    const std::size_t slash = r.find_last_of(L"\\/");
    std::wstring leaf = slash == std::wstring::npos ? r : r.substr(slash + 1);
    for (auto& c : leaf) c = wchar_t(towlower(c));
    return leaf == L"annotations" ? r + GateSuffix : L"";
}

// RAII gate. Shared: Busy() when the helper holds it exclusively; any other open failure (read-only folder,
// no gate for a custom store) means no migration can run there either, so the request proceeds.
class Gate {
public:
    Gate() = default;
    Gate(const std::wstring& path, bool exclusive, DWORD waitMs = 0) { Open(path, exclusive, waitMs); }
    ~Gate() { Close(); }
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    void Open(const std::wstring& path, bool exclusive, DWORD waitMs = 0) {
        Close();
        if (path.empty()) return;
        const ULONGLONG end = GetTickCount64() + waitMs;
        for (;;) {
            h_ = CreateFileW(path.c_str(), GENERIC_READ, exclusive ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h_ != INVALID_HANDLE_VALUE) return;
            const DWORD e = GetLastError();
            if (e != ERROR_SHARING_VIOLATION && e != ERROR_LOCK_VIOLATION) return;  // not a held gate
            if (GetTickCount64() >= end) {
                busy_ = true;
                return;
            }
            Sleep(50);
        }
    }
    void Close() {
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
        busy_ = false;
    }
    bool Held() const { return h_ != INVALID_HANDLE_VALUE; }
    bool Busy() const { return busy_; }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
    bool busy_ = false;
};

}  // namespace AnnotationsMigration
