#pragma once
// Same-window profile workspaces: the cache policy (pure, no Win32). The integration calls these two
// functions directly; tests/profiles_test.cpp tests the same code (no separate model).
//
//  * A window keeps at most `bound` LIVE workspaces (its WebView2 environment + controllers alive): the
//    shown one plus the most recently used others. Beyond that, the least recently used live ones become
//    COLD (session saved, environment released, session claim kept: their tabs come back from disk).
//  * A PINNED workspace (downloads in progress, a pending permission / dialog / authentication) is never
//    made cold: the window may exceed the bound until it is no longer pinned.
//  * The shown workspace is never made cold.
#include <algorithm>
#include <string>
#include <vector>

namespace Profiles {
namespace WorkspaceCache {

inline constexpr int DefaultLive = 3;   // shown + 2 parked

struct Entry { std::wstring id; unsigned long long lastUsed = 0; bool live = false; bool pinned = false; };

// Live workspaces to make cold after `active` is shown, least recently used first, so that the live
// count (the shown one always counts) is <= bound. May return fewer when pinned ones prevent it.
inline std::vector<std::wstring> ToCold(const std::vector<Entry>& entries, const std::wstring& active, int bound = DefaultLive) {
    bound = std::max(bound, 1);
    int live = 0; bool activeSeen = false; std::vector<const Entry*> candidates;
    for (const auto& e : entries) {
        if (e.id == active) { activeSeen = true; ++live; continue; }
        if (!e.live) continue;
        ++live; if (!e.pinned) candidates.push_back(&e);
    }
    if (!activeSeen && !active.empty()) ++live;   // being created
    std::sort(candidates.begin(), candidates.end(), [](const Entry* a, const Entry* b) { return a->lastUsed != b->lastUsed ? a->lastUsed < b->lastUsed : a->id < b->id; });
    std::vector<std::wstring> out;
    for (const Entry* e : candidates) { if (live <= bound) break; out.push_back(e->id); --live; }
    return out;
}

// The workspace to show when `leaving` goes away (deleted, evicted): the most recently used other one
// (a live one before a cold one used at the same time). Empty when there is none: the window closes.
inline std::wstring Successor(const std::vector<Entry>& entries, const std::wstring& leaving) {
    const Entry* best = nullptr;
    for (const auto& e : entries) {
        if (e.id == leaving) continue;
        if (!best || e.lastUsed > best->lastUsed || (e.lastUsed == best->lastUsed && e.live && !best->live) || (e.lastUsed == best->lastUsed && e.live == best->live && e.id < best->id)) best = &e;
    }
    return best ? best->id : std::wstring();
}

} // namespace WorkspaceCache
} // namespace Profiles
