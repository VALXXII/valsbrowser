#pragma once
// Annotations taken in a private window (#125, Val's decision of 2026-11-24): like the rest of private browsing, they
// are erased when that private window closes; a private window that did not close normally (crash, power cut) leaves
// them to the next start of the browser, which erases them. Normal windows' annotations are never touched.
//
// The notes themselves stay in the ONE store (src/annotations.h), so a connected agent reads them while the private
// window is open. What marks them private is a ledger beside the store, one per private window process:
//   <data root>\annotations-private\<random>.ledger     one line per annotation: "<siteKey>\t<id>\n"
// The process keeps its ledger open (write, shared for reading only) for as long as it lives. Windows closes that
// handle when the process ends, however it ends: a ledger that can be opened with no sharing at all belongs to a
// process that is gone. Its annotations are deleted (published, draft and trash copies), then the ledger.
// The line is written BEFORE the annotation (a crash between the two leaves a line for nothing, never a note without
// its line). No Win32 window, no thread: the host calls Add / Erase on its UI thread and CleanOrphans off it.
// Lot R (decision A, 2026-11-24): the chat works in a private window too. What it keeps there — its conversations,
// the site -> folder memory, the copies of the project's files taken before each turn (« Tout annuler ») — goes into
// ONE folder beside the ledger, erased with it, at the close as after a crash:
//   <data root>\annotations-private\<random>.data\      (same <random> as the window's ledger)
// Tests: tests/annotations/annotation_private_test.cpp.
#include <windows.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "annotations.h"

namespace AnnotationPrivate {

struct Report { std::size_t ledgers = 0, deleted = 0, missing = 0, failed = 0; bool complete = true; };

namespace detail {
inline std::vector<std::pair<std::string, std::string>> Parse(const std::string& bytes) {
    std::vector<std::pair<std::string, std::string>> out; std::size_t at = 0;
    while (at < bytes.size()) {
        std::size_t end = bytes.find('\n', at); if (end == std::string::npos) end = bytes.size();
        const std::string line = bytes.substr(at, end - at); at = end + 1;
        const auto tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;   // a line cut by a crash: nothing to delete yet
        out.push_back({line.substr(0, tab), line.substr(tab + 1)});
    }
    return out;
}
inline std::string ReadAll(HANDLE h) {
    std::string bytes; LARGE_INTEGER size{}; if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > (8 << 20)) return bytes;
    LARGE_INTEGER zero{}; SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
    bytes.resize(static_cast<std::size_t>(size.QuadPart)); DWORD got = 0;
    if (!ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr)) got = 0;
    bytes.resize(got); return bytes;
}
// Every copy of one annotation: published, draft, in the trash. A copy that is not there is fine.
inline void DeleteEntry(const BrowserAnnotations::Store& store, const std::string& site, const std::string& id, Report& r) {
    namespace BA = BrowserAnnotations;
    bool any = false, failed = false;
    for (bool draft : {false, true}) {
        const BA::Outcome o = store.Delete(site, id, draft);
        if (o.ok()) any = true; else if (o.code != BA::Code::NotFound) failed = true;
    }
    const std::wstring trashed = store.Root() + L"\\trash\\" + std::wstring(site.begin(), site.end()) + L"\\" + std::wstring(id.begin(), id.end());
    if (GetFileAttributesW(trashed.c_str()) != INVALID_FILE_ATTRIBUTES) any = true;   // « Supprimer » in the history moved it there
    const BA::Outcome t = store.DiscardTrash(site, id); if (!t.ok()) failed = true;
    if (failed) ++r.failed; else if (any) ++r.deleted; else ++r.missing;
    if (failed) r.complete = false;
}
// A folder and everything under it. A link (junction, symbolic link) met on the way is removed as a link: what it
// points to is never entered nor deleted. True when nothing is left.
inline bool RemoveTree(const std::wstring& dir) {
    const DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return true;
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) { SetFileAttributesW(dir.c_str(), FILE_ATTRIBUTE_NORMAL); return DeleteFileW(dir.c_str()) != 0; }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return RemoveDirectoryW(dir.c_str()) != 0;
    bool ok = true; std::vector<std::pair<std::wstring, DWORD>> entries;
    WIN32_FIND_DATAW fd{}; HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do { const std::wstring name = fd.cFileName; if (name != L"." && name != L"..") entries.push_back({name, fd.dwFileAttributes}); } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    for (const auto& e : entries) {
        const std::wstring path = dir + L"\\" + e.first;
        if (e.second & FILE_ATTRIBUTE_DIRECTORY) { if (!RemoveTree(path)) ok = false; }
        else { if (e.second & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); if (!DeleteFileW(path.c_str())) ok = false; }
    }
    return RemoveDirectoryW(dir.c_str()) != 0 && ok;
}
// <dir>\<random>.ledger -> <dir>\<random>.data
inline std::wstring DataDirOf(const std::wstring& ledgerPath) {
    const std::wstring ext = L".ledger";
    if (ledgerPath.size() <= ext.size() || ledgerPath.compare(ledgerPath.size() - ext.size(), ext.size(), ext) != 0) return L"";
    return ledgerPath.substr(0, ledgerPath.size() - ext.size()) + L".data";
}
inline std::wstring RandomName() {
    const std::string id = BrowserAnnotations::NewAnnotationId();   // 130 random bits, [a-z0-9_]
    return std::wstring(id.begin(), id.end()) + L".ledger";
}
}

// The ledger of THIS private window process (created at its first annotation).
class Ledger {
    HANDLE file_ = INVALID_HANDLE_VALUE; std::wstring path_;
public:
    Ledger() = default; Ledger(const Ledger&) = delete; Ledger& operator=(const Ledger&) = delete;
    ~Ledger() { Close(); }
    bool Opened() const { return file_ != INVALID_HANDLE_VALUE; }
    const std::wstring& Path() const { return path_; }
    // This window's own folder for what its chat keeps (created at the first call). Empty: no ledger, or no folder.
    std::wstring DataDir() const {
        if (!Opened()) return L"";
        const std::wstring dir = detail::DataDirOf(path_); if (dir.empty()) return L"";
        CreateDirectoryW(dir.c_str(), nullptr);
        const DWORD attrs = GetFileAttributesW(dir.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT) ? dir : std::wstring();
    }
    // Creates <dir>\<random>.ledger (and <dir>). False: no ledger (the caller then refuses to write the note).
    bool Open(const std::wstring& dir) {
        if (Opened()) return true;
        CreateDirectoryW(dir.c_str(), nullptr);
        const DWORD attrs = GetFileAttributesW(dir.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        for (int tries = 0; tries < 4 && !Opened(); ++tries) {
            const std::wstring path = dir + L"\\" + detail::RandomName();
            file_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (Opened()) path_ = path;
        }
        return Opened();
    }
    // One annotation of this window, written (and flushed) before the annotation itself.
    bool Add(const std::string& siteKey, const std::string& id) {
        if (!Opened() || siteKey.empty() || id.empty() || siteKey.find_first_of("\t\n") != std::string::npos || id.find_first_of("\t\n") != std::string::npos) return false;
        const std::string line = siteKey + "\t" + id + "\n";
        LARGE_INTEGER zero{}; SetFilePointerEx(file_, zero, nullptr, FILE_END);
        DWORD wrote = 0;
        return WriteFile(file_, line.data(), static_cast<DWORD>(line.size()), &wrote, nullptr) && wrote == line.size() && FlushFileBuffers(file_);
    }
    std::vector<std::pair<std::string, std::string>> Entries() const { return Opened() ? detail::Parse(detail::ReadAll(file_)) : std::vector<std::pair<std::string, std::string>>{}; }
    // The private window closes: every annotation it took is deleted. The ledger goes too when nothing was left
    // behind; otherwise it stays (closed) for the next start's CleanOrphans.
    // keepLedger: a note may still be on its way to the disk (the host could not wait for it): the ledger stays.
    Report Erase(const BrowserAnnotations::Store& store, bool keepLedger = false) {
        Report r; if (!Opened()) return r;
        r.ledgers = 1;
        for (const auto& e : Entries()) detail::DeleteEntry(store, e.first, e.second, r);
        if (!keepLedger && !detail::RemoveTree(detail::DataDirOf(path_))) { r.complete = false; ++r.failed; }   // the chat's folder of this window
        CloseHandle(file_); file_ = INVALID_HANDLE_VALUE;
        if (keepLedger) r.complete = false;
        if (r.complete) DeleteFileW(path_.c_str());
        return r;
    }
    // Leaves the ledger on disk as it is (the next start erases its annotations).
    void Close() { if (Opened()) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; } }
};

// Next start (any window): the ledgers no live process holds. A ledger still open by a running private window is
// left alone. Missing folder: nothing to do.
inline Report CleanOrphans(const std::wstring& dir, const BrowserAnnotations::Store& store) {
    Report r;
    WIN32_FIND_DATAW fd{}; HANDLE find = FindFirstFileW((dir + L"\\*.ledger").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) { RemoveDirectoryW(dir.c_str()); return r; }   // an empty folder goes (only when empty)
    std::vector<std::wstring> names;
    do { if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) names.push_back(fd.cFileName); } while (FindNextFileW(find, &fd));
    FindClose(find);
    for (const auto& name : names) {
        const std::wstring path = dir + L"\\" + name;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;   // its private window is still open (or the file is busy): next time
        ++r.ledgers; Report one;
        for (const auto& e : detail::Parse(detail::ReadAll(h))) detail::DeleteEntry(store, e.first, e.second, one);
        if (!detail::RemoveTree(detail::DataDirOf(path))) { one.complete = false; ++one.failed; }   // what that window's chat kept
        CloseHandle(h);
        r.deleted += one.deleted; r.missing += one.missing; r.failed += one.failed;
        if (one.complete) DeleteFileW(path.c_str()); else r.complete = false;
    }
    RemoveDirectoryW(dir.c_str());   // only when empty
    return r;
}

} // namespace AnnotationPrivate
