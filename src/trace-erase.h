#pragma once
// « Effacer les données » (A5) for a period and categories, across windows (review S5, coordinator):
// the erase is a DELETION DELTA applied under the profile lock to the CURRENT file, not to this
// window's memory: entries another window added after this window loaded (and that this window never
// saw) are erased too when they fall in the period; nothing outside the period or the categories is
// touched; this window's own pending changes are merged first so none is lost or resurrected.
// Outcomes are honest: Busy (lock), UnknownFormat (not a file this build can filter: nothing written),
// Failed (read/write); only Done means the selection is gone from disk.
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
#include "address.h"
#include "downloads.h"
#include "profile-store.h"

namespace TraceErase {

enum class Outcome { Done, Busy, Failed, UnknownFormat };

// History: visits whose last time is >= since (since <= 0: all) are erased.
inline Outcome History(const std::wstring& dataDir, const std::wstring& path, std::int64_t since, AddressBar::History& mine, AddressBar::History& base, DWORD timeoutMs = 5000) {
    ProfileStore::Lock lock(dataDir, timeoutMs);
    if (!lock.Held()) return Outcome::Busy;
    std::string bytes; if (!ProfileStore::ReadAll(path, bytes)) return Outcome::Failed;
    if (!bytes.empty() && ProfileStore::FirstLine(bytes) != "browser-history 1") return Outcome::UnknownFormat;
    std::istringstream in(bytes); const AddressBar::History theirs = AddressBar::History::Read(in);
    if (!bytes.empty() && ProfileStore::RecordLines(bytes) != theirs.Items().size()) return Outcome::Failed;   // damaged: never rewritten from a partial read
    AddressBar::History merged = AddressBar::History::Merge(base, mine, theirs);
    std::vector<std::wstring> gone; for (const auto& v : merged.Items()) if (since <= 0 || v.last >= since) gone.push_back(v.url);
    for (const auto& u : gone) merged.Remove(u);
    std::ostringstream out; merged.Write(out);
    if (!ProfileStore::WriteAtomic(path, out.str())) return Outcome::Failed;
    mine = merged; base = merged;
    return Outcome::Done;
}

// Finished downloads started at or after `since` (or with an unknown start; since <= 0: all) are erased;
// this window's live rows stay.
inline Outcome Downloads(const std::wstring& dataDir, const std::wstring& path, std::int64_t since, std::vector<NativeChrome::Downloads::Item>& mine, std::vector<NativeChrome::Downloads::Item>& base, int& nextId, DWORD timeoutMs = 5000) {
    namespace DL = NativeChrome::Downloads;
    ProfileStore::Lock lock(dataDir, timeoutMs);
    if (!lock.Held()) return Outcome::Busy;
    std::string bytes; if (!ProfileStore::ReadAll(path, bytes)) return Outcome::Failed;
    const std::string head = ProfileStore::FirstLine(bytes);
    if (!bytes.empty() && head != "browser-downloads 2" && head != "browser-downloads 1") return Outcome::UnknownFormat;
    int ignored = 0; std::istringstream in(bytes); const auto theirs = DL::ReadHistory(in, ignored);
    if (!bytes.empty() && ProfileStore::RecordLines(bytes) != theirs.size()) return Outcome::Failed;   // damaged: never rewritten from a partial read
    auto merged = DL::MergeHistory(base, mine, theirs, nextId);
    merged.erase(std::remove_if(merged.begin(), merged.end(), [since](const DL::Item& d) { return DL::Persistent(d) && (since <= 0 || d.started >= since || d.started == 0); }), merged.end());
    std::ostringstream out; DL::WriteHistory(out, merged);
    if (!ProfileStore::WriteAtomic(path, out.str())) return Outcome::Failed;
    mine = merged; base.clear(); for (const auto& d : merged) if (DL::Persistent(d)) base.push_back(d);
    return Outcome::Done;
}

} // namespace TraceErase
