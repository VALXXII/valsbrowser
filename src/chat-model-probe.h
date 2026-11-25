#pragma once
// Chat intégré — #128 v2: the model list of an agent the user has not started here yet, read from the agent itself.
//
// Why: the host learns an agent's models only from a session of that agent (its `configOptions` at session/new, then the
// R13 cache). A profile that never ran Codex had no list at all, and the model menu showed Codex as one row « Modèle par
// défaut » (#129's fallback) instead of its real models. Both adapters (claude-agent-acp 0.107, codex-acp 2.6) report
// their models the same way, as a `configOptions` entry of category "model" in the session/new answer.
//
// What it does (the coordinator's limits, then Val's decision « option A », 24 Nov 2026): for an agent installed AND
// signed in, whose models are unknown, while no chat session of that agent runs: when its adapter is not here yet, it is
// prepared first — the private Node and the pinned adapter downloaded into <data>gents exactly as at the agent's first
// use (AgentRuntime::Prepare, the same lock as the panel's own preparation, logged the same way); then the adapter
// starts, `initialize`, `session/new` in a folder of its own under %TEMP% (nothing in the user's projects; removed after),
// its `configOptions` are read, and it stops. Never a prompt, never `authenticate`, no MCP server, no resume. In the
// background: the menu shows the built-in list (chat-known-models.h) meanwhile and the agent's own one when it comes.
// Once per launch when it works; a failed download or read is tried again at the next detection of the agents (the
// next opening of the chat or start of the mode), at most MaxTries times per launch, and at every launch. The list is
// then cached like any other (R13). A session/new is no model call: the adapters answer it
// from their CLI's model catalogue (codex app-server `thread/start` + `model/list`; the Claude Agent SDK's
// `supportedModels`), no inference, nothing counted against the user's usage by the agents themselves.
#include "acp-client.h"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace ChatModelProbe {

// Val's decision (24 Nov 2026, option A): an agent installed and signed in whose adapter is not here yet gets it prepared
// (downloaded, as at its first use) for the read. false: only agents already prepared are read (the earlier limit).
inline constexpr bool PrepareForModels = true;
inline constexpr int MaxTries = 3;   // failed downloads / reads per launch (each at a later detection, never in a loop)

// When the browser may read an agent's model list on its own (pure: unit-tested).
struct Facts {
    bool installed = false;           // its CLI is found
    bool prepared = false;            // the private Node and the adapter are in place (nothing to download)
    bool signedIn = false;            // its CLI's status says signed in (not « unknown », not signed out)
    bool modelsKnown = false;         // a model option is already known (a session, the R13 cache, an earlier read)
    bool readThisLaunch = false;      // settled since the browser started: read, or failed MaxTries times (Settled)
    bool sessionOfThisAgent = false;  // a chat session of this agent is starting or running
    bool reading = false;             // a read is running (one at a time)
};
enum class Action { None, Read, PrepareThenRead };
inline Action Decide(const Facts& f, bool mayPrepare = PrepareForModels) {
    if (!f.installed || !f.signedIn || f.modelsKnown || f.readThisLaunch || f.sessionOfThisAgent || f.reading) return Action::None;
    if (f.prepared) return Action::Read;
    return mayPrepare ? Action::PrepareThenRead : Action::None;
}
inline bool ShouldRead(const Facts& f, bool mayPrepare = PrepareForModels) { return Decide(f, mayPrepare) != Action::None; }
// After a read: settled for this launch when it worked (even with no model list: nothing more to ask), or after MaxTries
// failures; else it is tried again at the next detection.
inline bool Settled(bool worked, int failures) { return worked || failures >= MaxTries; }
// The one lock around AgentRuntime::Prepare in this process: the background preparation and the panel's own (a session
// that starts) never write <data>gents at the same time — the second one waits, then finds everything in place.
inline std::mutex& PrepareLock() { static std::mutex m; return m; }

// A folder of its own for the read's session (the adapter needs an absolute cwd): %TEMP%\browser-models-<pid>-<n>.
inline std::wstring MakeFolder() {
    wchar_t tmp[MAX_PATH + 1] = L"";
    const DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (!n || n > MAX_PATH) return L"";
    static LONG serial = 0;
    for (int i = 0; i < 20; ++i) {
        std::wstring dir = std::wstring(tmp) + L"browser-models-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(InterlockedIncrement(&serial));
        if (CreateDirectoryW(dir.c_str(), nullptr)) return dir;
        if (GetLastError() != ERROR_ALREADY_EXISTS) return L"";
    }
    return L"";
}
inline bool RemoveFolder(const std::wstring& dir) {
    if (dir.empty()) return true;
    std::error_code ec; std::filesystem::remove_all(std::filesystem::path(dir), ec);
    return !ec && GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES;
}

struct Result {
    bool ok = false;                         // the session opened: `options` is what the agent said
    std::vector<Acp::ConfigOption> options;  // its configOptions (models, efforts, modes…)
    std::string why;                         // when not ok: the start's error code, "timeout", "start refused"
    bool stopped = false;                    // its processes are gone
    bool prepareTried = false, prepared = false;   // the adapter had to be prepared first; it is now
    std::string prepareLog;                  // that preparation's result, as the panel's own logs it (PrepareErrorName — detail)
};
// Blocking — a thread of its own, never the UI thread. Start → initialize → session/new in `o.cwd` → its configOptions →
// stop (the adapter and everything it started). `o.resolve` and `o.cwd` are the caller's; whatever else `o` asks for that
// would do more than open a session (resume, sign-in, MCP servers, questions) is cleared. `prepare` (PrepareThenRead): run
// first, under PrepareLock; it returns false with its log line when the adapter could not be made ready (no session then).
inline Result Read(Acp::StartOptions o, unsigned timeoutMs = 90000, const std::function<bool(std::string& log)>& prepare = nullptr) {
    Result r;
    if (prepare) {
        r.prepareTried = true;
        std::lock_guard<std::mutex> g(PrepareLock());
        r.prepared = prepare(r.prepareLog);
        if (!r.prepared) { r.why = "prepare: " + r.prepareLog; r.stopped = true; return r; }
    }
    o.resumeSessionId.clear(); o.authenticateMethodId.clear(); o.mcpServers.clear(); o.questions = false;
    Acp::Session s;
    if (!s.Start(std::move(o))) { r.why = "start refused"; return r; }
    const ULONGLONG until = GetTickCount64() + timeoutMs;
    std::vector<Acp::Event> events;
    for (bool started = false; !started;) {
        const ULONGLONG now = GetTickCount64();
        if (now >= until) { r.why = "timeout"; break; }
        s.WaitForEvents(unsigned(std::min<ULONGLONG>(until - now, 500)));
        events.clear(); s.Poll(events);
        for (auto& e : events) {
            if (e.type != Acp::Event::Type::Started) continue;
            started = true;
            if (e.started.state == Acp::StartState::Ready) { r.ok = true; r.options = std::move(e.started.configOptions); }
            else r.why = Acp::ErrorCodeName(e.started.error.code);
            break;
        }
    }
    s.Dispose();
    r.stopped = s.WaitStopped(10000);
    return r;
}

}  // namespace ChatModelProbe
