#pragma once
// One install/removal transaction at a time per install root, and the durable repair record.
// Used by update-helper.exe (installs from the engine or the setup, launcher recovery) and by the setup /
// uninstaller (src/setup-core.cpp). Design: design/windows-setup-plan.md, « Transactions ».
//
// Lock: <root>\install.lock opened with NO sharing (in particular no FILE_SHARE_DELETE). While any process holds it:
//   * every other installer, uninstaller and helper fails to open it (Busy) -> no concurrent mutation of the root;
//   * NTFS refuses to rename the root or ANY of its ancestors (a file below them is open), so the path validated at
//     acquisition (final path == lexical path: no junction/symlink anywhere above or at the root) cannot be swapped
//     for a junction while the transaction runs.
// The setup opens it inheritable and hands it to the helper it starts (plan line `lock-handle <n>`, checked by final
// path), so exclusion lasts until the helper exits even if the setup dies. Engine-started helpers acquire it
// themselves. Processes started BY the helper never inherit it.
//
// Repair record: <root>\updates\repair-transaction, written before the live versions\<v> is renamed aside and removed
// only once the outcome is final (rolled back, or committed after a no-launch success or the new process's ack). A
// helper killed anywhere in between leaves the record; RecoverRepair (under the lock) finishes the transaction:
// commit if the repaired version acknowledged its start (ack file of that nonce), else restore the original copy.
// The aside is never deleted unless the original is no longer needed.
#include <windows.h>

#include <string>

#include "update-package.h"

namespace InstallTxn {

constexpr const wchar_t* LockName = L"install.lock";
constexpr const wchar_t* RepairRecordName = L"repair-transaction";

inline std::wstring LowerPath(std::wstring s) {
    CharLowerBuffW(s.data(), DWORD(s.size()));
    return s;
}

// Normalised absolute path without trailing separator ("" on failure).
inline std::wstring Absolute(const std::wstring& p) {
    wchar_t buffer[4096];
    DWORD n = GetFullPathNameW(p.c_str(), 4096, buffer, nullptr);
    if (!n || n >= 4096) return L"";
    std::wstring s(buffer, n);
    while (s.size() > 3 && s.back() == L'\\') s.pop_back();
    return s;
}

// Final path of an open handle, lower-case, without the \\?\ prefix ("" on failure).
inline std::wstring HandlePath(HANDLE h) {
    std::wstring buffer(32768, L'\0');
    DWORD n = GetFinalPathNameByHandleW(h, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= buffer.size()) return L"";
    buffer.resize(n);
    if (buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) buffer = L"\\\\" + buffer.substr(8);
    else if (buffer.rfind(L"\\\\?\\", 0) == 0) buffer = buffer.substr(4);
    return LowerPath(buffer);
}

// True when `path` exists and the object it names resolves to exactly that lexical path (no reparse point at it or
// at any ancestor, no 8.3 alias). `directory` selects the expected kind.
inline bool ResolvesToItself(const std::wstring& path, bool directory) {
    const std::wstring abs = Absolute(path);
    if (abs.empty()) return false;
    HANDLE h = CreateFileW(abs.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    FILE_ATTRIBUTE_TAG_INFO tag{};
    const bool kind = GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) &&
                      !(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && bool(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
    const std::wstring final_ = HandlePath(h);
    CloseHandle(h);
    wchar_t longPath[4096];  // an 8.3 alias in the lexical path is not a redirection
    const DWORD n = GetLongPathNameW(abs.c_str(), longPath, 4096);
    const std::wstring lexical = n && n < 4096 ? std::wstring(longPath, n) : abs;
    return kind && final_ == LowerPath(lexical);
}

class Lock {
public:
    Lock() = default;
    ~Lock() { Release(); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

    // Opens <root>\install.lock exclusively, retrying until waitMs (0 = once). The root must already exist and must
    // resolve to itself (no reparse point at or above it): otherwise false with busy = false.
    bool Acquire(const std::wstring& root, DWORD waitMs, bool inheritable = false) {
        Release();
        busy_ = false;
        const std::wstring lock = Absolute(root) + L"\\" + LockName;
        if (!ResolvesToItself(root, true)) return false;
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, inheritable ? TRUE : FALSE};
        const ULONGLONG end = GetTickCount64() + waitMs;
        for (;;) {
            h_ = CreateFileW(lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &sa, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (h_ != INVALID_HANDLE_VALUE) break;
            const DWORD e = GetLastError();
            busy_ = e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION;
            if (!busy_ || GetTickCount64() >= end) return false;
            Sleep(100);
        }
        // Now that nothing above can be renamed, the lock must be exactly <root>\install.lock, a plain file.
        if (!Verify(root)) {
            Release();
            return false;
        }
        return true;
    }
    // A lock handle inherited from the setup that started this process: accepted only if it is exactly this root's.
    bool Adopt(HANDLE inherited, const std::wstring& root) {
        Release();
        h_ = inherited;
        if (Verify(root)) return true;
        h_ = INVALID_HANDLE_VALUE;  // not ours: never close a handle we cannot vouch for
        return false;
    }
    void Release() {
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }
    bool Held() const { return h_ != INVALID_HANDLE_VALUE; }
    bool Busy() const { return busy_; }
    HANDLE Handle() const { return h_; }

private:
    bool Verify(const std::wstring& root) const {
        FILE_ATTRIBUTE_TAG_INFO tag{};
        if (!GetFileInformationByHandleEx(h_, FileAttributeTagInfo, &tag, sizeof(tag)) ||
            (tag.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
            return false;
        if (!ResolvesToItself(root, true)) return false;
        wchar_t longPath[4096];
        const std::wstring abs = Absolute(root);
        const DWORD n = GetLongPathNameW(abs.c_str(), longPath, 4096);
        const std::wstring lexical = n && n < 4096 ? std::wstring(longPath, n) : abs;
        return HandlePath(h_) == LowerPath(lexical + L"\\" + LockName);
    }
    HANDLE h_ = INVALID_HANDLE_VALUE;
    bool busy_ = false;
};

// ---------------------------------------------------------------- repair record
struct Repair {
    std::string nonce, version;
};

inline std::wstring RepairRecord(const std::wstring& root) { return root + L"\\updates\\" + RepairRecordName; }
inline std::wstring RepairAside(const std::wstring& root, const std::string& nonce) {
    return root + L"\\versions\\.repair-" + UpdatePackage::Widen(nonce);
}

inline bool WriteRepair(const std::wstring& root, const Repair& r) {
    return UpdatePackage::AtomicWriteFile(RepairRecord(root), "browser-repair 1\nnonce " + r.nonce + "\nversion " + r.version + "\n");
}

inline bool ReadRepair(const std::wstring& root, Repair& r) {
    std::string text;
    if (!UpdatePackage::ReadSmallFile(RepairRecord(root), 4096, text)) return false;
    auto field = [&](const std::string& key) {
        const std::string k = "\n" + key + " ";
        const auto at = text.find(k);
        if (at == std::string::npos) return std::string();
        const auto end = text.find('\n', at + k.size());
        return text.substr(at + k.size(), end == std::string::npos ? std::string::npos : end - at - k.size());
    };
    if (text.rfind("browser-repair 1\n", 0) != 0) return false;
    r.nonce = field("nonce");
    r.version = field("version");
    UpdatePackage::Version v;
    return r.nonce.size() == 32 && r.nonce.find_first_not_of("0123456789abcdef") == std::string::npos &&
           UpdatePackage::ParseVersion(r.version, v);
}

enum class RepairOutcome { None, Committed, RolledBack, Kept };

// Puts the original copy back: the replacement (if any) is moved out of the way first; if anything fails the aside and
// the record stay for the next attempt. Caller holds the Lock.
inline RepairOutcome RollbackRepair(const std::wstring& root, const Repair& r) {
    namespace UP = UpdatePackage;
    const std::wstring aside = RepairAside(root, r.nonce), target = root + L"\\versions\\" + UP::Widen(r.version);
    const std::wstring failed = root + L"\\versions\\.repair-failed-" + UP::Widen(r.nonce);
    if (!UP::PathExists(aside)) {  // nothing saved (rename never happened): the record alone is stale
        DeleteFileW(RepairRecord(root).c_str());
        return RepairOutcome::RolledBack;
    }
    if (UP::IsReparsePoint(aside)) return RepairOutcome::Kept;
    if (UP::PathExists(target) && (UP::IsReparsePoint(target) || !UP::MoveDirectory(target, failed, 15000))) return RepairOutcome::Kept;
    if (!UP::MoveDirectory(aside, target, 15000)) {
        if (!UP::PathExists(target) && UP::PathExists(failed)) UP::MoveDirectory(failed, target, 15000);  // keep a usable copy in place
        return RepairOutcome::Kept;
    }
    UP::RemoveTree(failed);
    DeleteFileW(RepairRecord(root).c_str());
    return RepairOutcome::RolledBack;
}

// The repaired copy is final: the original is no longer needed. Caller holds the Lock.
inline RepairOutcome CommitRepair(const std::wstring& root, const Repair& r) {
    namespace UP = UpdatePackage;
    const std::wstring aside = RepairAside(root, r.nonce);
    if (UP::PathExists(aside) && !UP::IsReparsePoint(aside)) UP::RemoveTree(aside);
    if (UP::PathExists(aside)) return RepairOutcome::Kept;  // still in use: retried at the next recovery
    DeleteFileW(RepairRecord(root).c_str());
    return RepairOutcome::Committed;
}

// Interrupted repair (helper killed, or an unconfirmed start). Commit only when the repaired version acknowledged its
// start for this nonce (ack file written by the new process itself, src/update-core.cpp NoteStartup); otherwise restore
// the original. Caller holds the Lock (no helper of this root is running).
inline RepairOutcome RecoverRepair(const std::wstring& root) {
    namespace UP = UpdatePackage;
    Repair r;
    if (!UP::PathExists(RepairRecord(root))) return RepairOutcome::None;
    if (!ReadRepair(root, r)) return RepairOutcome::Kept;  // unreadable: never guess, keep everything
    std::string ack;
    const std::wstring target = root + L"\\versions\\" + UP::Widen(r.version);
    if (UP::ReadSmallFile(root + L"\\updates\\ack-" + UP::Widen(r.nonce), 4096, ack) && ack.rfind("browser-ack 1\n", 0) == 0 &&
        ack.find("\nversion " + r.version + "\n") != std::string::npos && UP::PathExists(target)) {
        DeleteFileW((root + L"\\updates\\ack-" + UP::Widen(r.nonce)).c_str());
        return CommitRepair(root, r);
    }
    return RollbackRepair(root, r);
}

}  // namespace InstallTxn
