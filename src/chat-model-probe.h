#pragma once
// Chat intégré — #128 v2: the model list of an agent the user has not started here yet, read from the agent itself.
//
// Why: the host learns an agent's models only from a session of that agent (its `configOptions` at session/new, then the
// R13 cache). A profile that never ran Codex had no list at all, and the model menu showed Codex as one row « Modèle par
// défaut » (#129's fallback) instead of its real models. Both adapters (claude-agent-acp 0.107, codex-acp 2.6) report
// their models the same way, as a `configOptions` entry of category "model" in the session/new answer.
//
// What it does (the coordinator's limits, 24 Nov 2026): for an agent installed, prepared (its adapter is here: nothing is
// ever downloaded for it) and signed in, whose models are unknown, while no chat session of that agent runs — once per
// launch: the adapter starts, `initialize`, `session/new` in a folder of its own under %TEMP% (nothing in the user's
// projects; removed after), its `configOptions` are read, and it stops. Never a prompt, never `authenticate`, no MCP
// server, no resume. The list is then cached like any other (R13). A session/new is no model call: the adapters answer it
// from their CLI's model catalogue (codex app-server `thread/start` + `model/list`; the Claude Agent SDK's
// `supportedModels`), no inference, nothing counted against the user's usage by the agents themselves.
#include "acp-client.h"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace ChatModelProbe {

// When the browser may read an agent's model list on its own (pure: unit-tested).
struct Facts {
    bool installed = false;           // its CLI is found
    bool prepared = false;            // the private Node and the adapter are in place (nothing to download)
    bool signedIn = false;            // its CLI's status says signed in (not « unknown », not signed out)
    bool modelsKnown = false;         // a model option is already known (a session, the R13 cache, an earlier read)
    bool readThisLaunch = false;      // already read (or tried) since the browser started
    bool sessionOfThisAgent = false;  // a chat session of this agent is starting or running
    bool reading = false;             // a read is running (one at a time)
};
inline bool ShouldRead(const Facts& f) {
    return f.installed && f.prepared && f.signedIn && !f.modelsKnown && !f.readThisLaunch && !f.sessionOfThisAgent && !f.reading;
}

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
};
// Blocking — a thread of its own, never the UI thread. Start → initialize → session/new in `o.cwd` → its configOptions →
// stop (the adapter and everything it started). `o.resolve` and `o.cwd` are the caller's; whatever else `o` asks for that
// would do more than open a session (resume, sign-in, MCP servers, questions) is cleared.
inline Result Read(Acp::StartOptions o, unsigned timeoutMs = 90000) {
    Result r;
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
