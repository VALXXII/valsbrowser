// #128 v2 — the model list of an agent read before its first chat (src/chat-model-probe.h), against the fake ACP agent
// (tests/acp/fake_agent.cpp: never a real agent, never the network, never the user's accounts).
//
//   model_probe_test.exe <fake-acp-agent.exe> <scratch folder>
//
// Checks: when the browser may read (installed and signed in, models unknown, no session of that agent, one at a time;
// not prepared yet: prepared first — Val's option A — or nothing with the flag off); a failure is tried again, at most
// MaxTries times per launch; the preparation runs first, under the one lock, and a failed one opens no session; the
// built-in model lists are the real adapters' (the recorded fixture) and give way to the agent's own; the read opens a session and nothing else (initialize + session/new, never session/prompt nor
// authenticate, no MCP server, no resume) in a folder of its own under %TEMP%, removed after; the agent's model list comes
// back; the agent and its children are stopped; an agent that is not signed in, or that fails, gives no list and no prompt.
#include "chat-model-probe.h"
#include "chat-known-models.h"
#include "chat-options.h"
#include <cstdio>
#include <thread>
#include <fstream>
#include <sstream>

static int g_checks = 0, g_failed = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_failed; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::wstring g_agent, g_tmp, g_root = L".";
static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr); return s;
}
static std::wstring FullPath(const std::wstring& p) { wchar_t b[MAX_PATH * 2]; const DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 2, b, nullptr); return n ? std::wstring(b, n) : p; }
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static std::string ReadAll(const std::wstring& p) { std::ifstream f(p.c_str(), std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

static Acp::StartOptions Options(const std::wstring& log, std::vector<std::wstring> extra = {}) {
    Acp::StartOptions o;
    o.clientName = "browser-test"; o.clientVersion = "9.9";
    // what Read() must clear: a resume, a sign-in, MCP servers — the read opens a plain new session only
    o.resumeSessionId = "sess-known"; o.authenticateMethodId = "fake-login";
    Acp::McpServer m; m.name = "should-not-go"; m.command = "x.exe"; o.mcpServers.push_back(m);
    std::vector<std::wstring> args = {L"--log", log, L"--resume"};
    args.insert(args.end(), extra.begin(), extra.end());
    o.resolve = [args](const Acp::ResolveContext&) {
        Acp::Resolution r; r.launch.exe = g_agent; r.launch.args = args; r.launch.freshUserEnvironment = false; return r;
    };
    return o;
}

static void TestDecision() {
    using ChatModelProbe::Facts; using ChatModelProbe::ShouldRead; using ChatModelProbe::Decide; using ChatModelProbe::Action;
    Facts ok; ok.installed = ok.prepared = ok.signedIn = true;
    CHECK(ShouldRead(ok) && Decide(ok) == Action::Read);
    auto with = [&](auto set) { Facts f = ok; set(f); return ShouldRead(f); };
    CHECK(!with([](Facts& f) { f.installed = false; }));           // no CLI: nothing to start
    {   Facts f = ok; f.prepared = false;                          // its adapter is not here yet (Val's option A: prepared first)
        CHECK(ChatModelProbe::PrepareForModels && Decide(f) == Action::PrepareThenRead && Decide(f, false) == Action::None);
        f.signedIn = false; CHECK(Decide(f) == Action::None);      // not signed in: nothing downloaded, nothing read
        f.signedIn = true; f.installed = false; CHECK(Decide(f) == Action::None); }
    CHECK(!with([](Facts& f) { f.signedIn = false; }));            // signed out, or not known: no sign-in prompt, no read
    CHECK(!with([](Facts& f) { f.modelsKnown = true; }));          // a session, the cache or an earlier read said them
    CHECK(!with([](Facts& f) { f.readThisLaunch = true; }));       // once per launch
    CHECK(!with([](Facts& f) { f.sessionOfThisAgent = true; }));   // never while a chat session of that agent starts or runs
    CHECK(!with([](Facts& f) { f.reading = true; }));              // one at a time
    // a failure is tried again at the next detection, at most MaxTries times per launch; a read that worked is settled
    CHECK(ChatModelProbe::Settled(true, 0) && !ChatModelProbe::Settled(false, 1) && !ChatModelProbe::Settled(false, ChatModelProbe::MaxTries - 1) && ChatModelProbe::Settled(false, ChatModelProbe::MaxTries));
}

// Val (24 Nov 2026): never « Modèle par défaut » — the built-in list, then the cache, then the agent's own list.
static void TestKnownModels() {
    const std::string fixture = ReadAll(g_root + L"\\tests\\chat\\fixtures\\real-adapter-models-2026-11-17.json");
    CHECK(!fixture.empty());
    for (const char* id : {"claude", "codex"}) {
        const auto* o = ChatKnownModels::For(id);
        CHECK(o && o->category == "model" && o->choices.size() >= 10);
        if (!o) continue;
        for (const auto& c : o->choices) {   // every id and name exactly as the real adapter reported it
            const std::size_t at = fixture.find("\"value\": \"" + c.value + "\""), nm = at == std::string::npos ? at : fixture.find("\"name\": \"", at);
            const bool found = nm != std::string::npos && fixture.compare(nm + 9, c.name.size() + 1, c.name + "\"") == 0;
            CHECK(found);
            if (!found) std::printf("  not in the fixture: %s / %s\n", c.value.c_str(), c.name.c_str());
        }
    }
    CHECK(fixture.find("\"currentValue\": \"" + ChatKnownModels::For("codex")->currentValue + "\"") != std::string::npos);
    CHECK(ChatKnownModels::For("other") == nullptr);
    // what is shown: the agent's own list (a session, the read, the cache) > the built-in one (installed, or not detected yet)
    std::vector<Acp::ConfigOption> none, own(1);
    own[0].id = "model"; own[0].category = "model"; own[0].type = "select"; own[0].choices = {{"opus", "Opus 5.5", "", ""}};
    CHECK(ChatKnownModels::Shown("codex", none, true, true) == ChatKnownModels::For("codex"));     // fresh profile, installed
    CHECK(ChatKnownModels::Shown("codex", none, false, false) == ChatKnownModels::For("codex"));   // not detected yet: never « Modèle par défaut »
    CHECK(ChatKnownModels::Shown("codex", none, true, false) == nullptr);                           // not installed: its header only
    CHECK(ChatKnownModels::Shown("claude", own, true, true) == &own[0]);                            // the cache / the live list wins
    CHECK(ChatKnownModels::IsBuiltIn("codex", ChatKnownModels::For("codex")) && !ChatKnownModels::IsBuiltIn("claude", &own[0]));
    // the Claude adapter's « default » alias names its recommended model: the built-in list's default is « opus »
    CHECK(ChatOptions::DefaultModel(ChatKnownModels::For("claude")->choices, std::string()) == "opus");
}

static void TestRead() {
    const std::wstring log = g_tmp + L"\\probe-ok.log"; DeleteFileW(log.c_str());
    const std::wstring dir = ChatModelProbe::MakeFolder();
    wchar_t tmp[MAX_PATH + 1] = L""; GetTempPathW(MAX_PATH, tmp);
    CHECK(!dir.empty() && Exists(dir) && dir.rfind(tmp, 0) == 0 && dir.find(L"browser-models-") != std::wstring::npos);   // under %TEMP%, its own
    const std::wstring other = ChatModelProbe::MakeFolder();
    CHECK(!other.empty() && other != dir); ChatModelProbe::RemoveFolder(other);
    Acp::StartOptions o = Options(log); o.cwd = Narrow(dir);
    const DWORD t0 = GetTickCount();
    const auto r = ChatModelProbe::Read(o, 20000);
    const DWORD took = GetTickCount() - t0;
    CHECK(r.ok && r.why.empty());
    const auto* mo = Acp::FindModelOption(r.options);
    CHECK(mo && mo->choices.size() >= 3);
    bool opus = false; if (mo) for (const auto& c : mo->choices) if (c.value == "opus") opus = true;
    CHECK(opus);
    CHECK(r.stopped);   // the agent and everything it started are gone
    const std::string said = ReadAll(log);
    CHECK(said.find("\"initialize\"") != std::string::npos && said.find("\"session/new\"") != std::string::npos);
    CHECK(said.find("session/prompt") == std::string::npos);          // never a message: nothing the agent would answer
    CHECK(said.find("\"authenticate\"") == std::string::npos);        // never a sign-in
    CHECK(said.find("session/resume") == std::string::npos && said.find("session/load") == std::string::npos);
    CHECK(said.find("should-not-go") == std::string::npos);           // no MCP server
    CHECK(said.find("cwd " + Narrow(dir)) != std::string::npos);      // its session folder is the read's own
    CHECK(ChatModelProbe::RemoveFolder(dir) && !Exists(dir));         // removed after
    std::printf("read: %zu models in %lu ms\n", mo ? mo->choices.size() : size_t(0), took);
}

static void TestPrepareThenRead() {
    const std::wstring dir = ChatModelProbe::MakeFolder();
    {   // prepared first, under the one lock (another thread cannot take it meanwhile), then the session and its models
        const std::wstring log = g_tmp + L"\\probe-prep.log"; DeleteFileW(log.c_str());
        Acp::StartOptions o = Options(log); o.cwd = Narrow(dir);
        bool ran = false, lockedMeanwhile = false;
        const auto r = ChatModelProbe::Read(o, 20000, [&](std::string& line) {
            ran = true;
            std::thread t([&] { if (ChatModelProbe::PrepareLock().try_lock()) ChatModelProbe::PrepareLock().unlock(); else lockedMeanwhile = true; }); t.join();
            line = "none"; return true; });
        CHECK(ran && lockedMeanwhile && r.prepareTried && r.prepared && r.prepareLog == "none" && r.ok && Acp::FindModelOption(r.options) && r.stopped);
        CHECK(ReadAll(log).find("session/prompt") == std::string::npos);
        // a model picked from the built-in list before the read: the live list decides (ChatOptions::ModelForNewSession) —
        // an id the agent has goes to its session, one it does not have is dropped (the session's own default then)
        const auto* live = Acp::FindModelOption(r.options);
        CHECK(live && ChatOptions::ModelForNewSession(live->choices, std::string("opus")) == "opus");
        CHECK(live && ChatOptions::ModelForNewSession(live->choices, std::string("claude-opus-4-6")).empty());
    }
    {   // a failed preparation (the network): no session at all, the reason kept; tried again later
        const std::wstring log = g_tmp + L"\\probe-prep-fail.log"; DeleteFileW(log.c_str());
        Acp::StartOptions o = Options(log); o.cwd = Narrow(dir);
        const auto r = ChatModelProbe::Read(o, 20000, [](std::string& line) { line = "network"; return false; });
        CHECK(!r.ok && r.prepareTried && !r.prepared && r.why == "prepare: network" && r.options.empty() && !Exists(log));
        const int failures = 1; CHECK(!ChatModelProbe::Settled(r.ok, failures));
        // the next try (the next detection): the agent answers this time — its models, settled
        const auto again = ChatModelProbe::Read(o, 20000, [](std::string& line) { line = "none"; return true; });
        CHECK(again.ok && Acp::FindModelOption(again.options) && ChatModelProbe::Settled(again.ok, failures));
    }
    {   // a read that fails (the agent crashes at session/new), then one that works
        const std::wstring log = g_tmp + L"\\probe-retry.log"; DeleteFileW(log.c_str());
        Acp::StartOptions bad = Options(log, {L"--crash-at", L"new"}); bad.cwd = Narrow(dir);
        const auto r1 = ChatModelProbe::Read(bad, 20000);
        Acp::StartOptions good = Options(log); good.cwd = Narrow(dir);
        const auto r2 = ChatModelProbe::Read(good, 20000);
        CHECK(!r1.ok && !ChatModelProbe::Settled(r1.ok, 1) && r2.ok && Acp::FindModelOption(r2.options));
    }
    CHECK(ChatModelProbe::RemoveFolder(dir) && !Exists(dir));
}

static void TestNotSignedIn() {
    const std::wstring log = g_tmp + L"\\probe-auth.log"; DeleteFileW(log.c_str());
    const std::wstring authFile = g_tmp + L"\\never-created.auth"; DeleteFileW(authFile.c_str());
    const std::wstring dir = ChatModelProbe::MakeFolder();
    Acp::StartOptions o = Options(log, {L"--auth-file", authFile}); o.cwd = Narrow(dir);
    const auto r = ChatModelProbe::Read(o, 20000);
    CHECK(!r.ok && r.options.empty() && r.why == "auth_required" && r.stopped);
    const std::string said = ReadAll(log);
    CHECK(said.find("\"authenticate\"") == std::string::npos && said.find("session/prompt") == std::string::npos && !Exists(authFile));
    CHECK(ChatModelProbe::RemoveFolder(dir) && !Exists(dir));
}

static void TestFailures() {
    const std::wstring log = g_tmp + L"\\probe-crash.log"; DeleteFileW(log.c_str());
    const std::wstring dir = ChatModelProbe::MakeFolder();
    Acp::StartOptions o = Options(log, {L"--crash-at", L"new"}); o.cwd = Narrow(dir);
    const auto r = ChatModelProbe::Read(o, 20000);
    CHECK(!r.ok && r.options.empty() && !r.why.empty() && r.stopped);
    {   // an agent that never answers: the read gives up at its time limit and stops it
        const std::wstring log2 = g_tmp + L"\\probe-silent.log"; DeleteFileW(log2.c_str());
        Acp::StartOptions s = Options(log2, {L"--silent"}); s.cwd = Narrow(dir);
        const DWORD t0 = GetTickCount();
        const auto q = ChatModelProbe::Read(s, 1500);
        CHECK(!q.ok && q.why == "timeout" && q.stopped && GetTickCount() - t0 < 15000);
    }
    CHECK(ChatModelProbe::RemoveFolder(dir) && !Exists(dir));
}

// « Ton agent » × #128 v2 (night of 25/11): the read waits for a calm chat — a start pushes it CalmMs further, a later
// start further again; nothing while the chat's own session starts or a turn runs.
static void TestCalm() {
    ChatModelProbe::Calm c;
    CHECK(c.Ready(1000, false, false));                                  // nothing started yet
    c.Busy(1000);                                                         // the annotation mode starts
    CHECK(!c.Ready(1000, false, false) && !c.Ready(1000 + ChatModelProbe::CalmMs - 1, false, false) && c.Wait(1000) == ChatModelProbe::CalmMs);
    CHECK(c.Ready(1000 + ChatModelProbe::CalmMs, false, false) && c.Wait(1000 + ChatModelProbe::CalmMs) == 0);
    c.Busy(3000);                                                         // a bubble opens meanwhile: the read waits again
    CHECK(!c.Ready(1000 + ChatModelProbe::CalmMs, false, false) && c.Ready(3000 + ChatModelProbe::CalmMs, false, false));
    CHECK(!c.Ready(99999, true, false) && !c.Ready(99999, false, true));  // the chat's own session starting, a turn: never
}
// The chat's own session start stops a read: before its session (no session opened) and while it waits for it.
static void TestCancel() {
    const std::wstring dir = ChatModelProbe::MakeFolder();
    {
        const std::wstring log = g_tmp + L"\probe-cancel-before.log"; DeleteFileW(log.c_str());
        Acp::StartOptions o = Options(log, {}); o.cwd = Narrow(dir);
        std::atomic<bool> cancel{true};
        const auto r = ChatModelProbe::Read(o, 20000, nullptr, &cancel);
        CHECK(!r.ok && r.cancelled && r.why == "cancelled" && r.stopped && !Exists(log));   // no agent started at all
    }
    {   // an agent that never answers: stopped at once on the chat's start, long before the read's time limit
        const std::wstring log = g_tmp + L"\probe-cancel-during.log"; DeleteFileW(log.c_str());
        Acp::StartOptions o = Options(log, {L"--silent"}); o.cwd = Narrow(dir);
        std::atomic<bool> cancel{false};
        std::thread later([&cancel] { Sleep(300); cancel.store(true); });
        const DWORD t0 = GetTickCount();
        const auto r = ChatModelProbe::Read(o, 60000, nullptr, &cancel);
        later.join();
        CHECK(!r.ok && r.cancelled && r.why == "cancelled" && r.stopped && GetTickCount() - t0 < 15000);
    }
    CHECK(ChatModelProbe::RemoveFolder(dir) && !Exists(dir));
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: model_probe_test <fake-acp-agent.exe> <scratch folder> [<repository>]\n"); return 2; }
    g_agent = FullPath(argv[1]); g_tmp = FullPath(argv[2]);
    CreateDirectoryW(g_tmp.c_str(), nullptr);
    if (!Exists(g_agent) || !Exists(g_tmp)) { std::printf("missing agent or folder\n"); return 2; }
    if (argc >= 4) g_root = FullPath(argv[3]);   // the repository: the fixture of the real adapters' lists
    TestDecision(); TestKnownModels(); TestRead(); TestPrepareThenRead(); TestNotSignedIn(); TestFailures(); TestCalm(); TestCancel();
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAILED" : "model_probe_test passed", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
