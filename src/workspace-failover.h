#pragma once
// Same-window profiles: where the window goes when a profile's engine fails (pure, no Win32; the integration in
// src/profile-workspace.h calls it directly and tests/ui/profile_ui_test.cpp tests the same code).
//
// A profile's engine failure stays LOCAL to that profile only when another profile of the window demonstrably
// works RIGHT NOW: started, not cold, not being created, not disposed, an engine AND a usable controller, and no
// failure of its own recently. Cold, unstarted (a restored slot) or recently failed profiles are never fallbacks
// (switching to them would start an engine that may fail the same way: a systemic WebView2 failure would bounce
// between profiles). With no such profile, the caller takes the normal recovery (Fail) once.
#include <string>
#include <vector>
namespace Profiles {
namespace WorkspaceFailover {
inline constexpr unsigned long long RecentFailureMs = 60000;   // a failure within this window disqualifies a fallback
struct Candidate {
    std::wstring id; unsigned long long lastUsed = 0;
    bool dead = false, started = false, cold = false, pending = false;
    bool engine = false, controller = false;    // an environment, and at least one tab with a live controller
    unsigned long long failedAt = 0;            // tick of its last engine failure (0 = never)
};
inline bool Healthy(const Candidate& c, unsigned long long now) {
    if (c.dead || !c.started || c.cold || c.pending || !c.engine || !c.controller) return false;
    return c.failedAt == 0 || now - c.failedAt >= RecentFailureMs;
}
// The working profile to show instead of `failing` (most recently used), or empty: no local failure possible.
inline std::wstring Target(const std::vector<Candidate>& all, const std::wstring& failing, unsigned long long now) {
    const Candidate* best = nullptr;
    for (const auto& c : all) {
        if (c.id == failing || !Healthy(c, now)) continue;
        if (!best || c.lastUsed > best->lastUsed || (c.lastUsed == best->lastUsed && c.id < best->id)) best = &c;
    }
    return best ? best->id : std::wstring();
}
} // namespace WorkspaceFailover
} // namespace Profiles
