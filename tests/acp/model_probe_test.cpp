// #128 v2 — the model list of an agent read before its first chat (src/chat-model-probe.h), against the fake ACP agent
// (tests/acp/fake_agent.cpp: never a real agent, never the network, never the user's accounts).
//
//   model_probe_test.exe <fake-acp-agent.exe> <scratch folder>
//
// Checks: when the browser may read (installed, prepared, signed in, models unknown, once per launch, no session of that
// agent, one at a time); the read opens a session and nothing else (initialize + session/new, never session/prompt nor
// authenticate, no MCP server, no resume) in a folder of its own under %TEMP%, removed after; the agent's model list comes
// back; the agent and its children are stopped; an agent that is not signed in, or that fails, gives no list and no prompt.
#include "chat-model-probe.h"
#include <cstdio>
#include <fstream>
#include <sstream>

static int g_checks = 0, g_failed = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_failed; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::wstring g_agent, g_tmp;
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
    using ChatModelProbe::Facts; using ChatModelProbe::ShouldRead;
    Facts ok; ok.installed = ok.prepared = ok.signedIn = true;
    CHECK(ShouldRead(ok));
    auto with = [&](auto set) { Facts f = ok; set(f); return ShouldRead(f); };
    CHECK(!with([](Facts& f) { f.installed = false; }));           // no CLI: nothing to start
    CHECK(!with([](Facts& f) { f.prepared = false; }));            // its adapter is not here: never downloaded for this
    CHECK(!with([](Facts& f) { f.signedIn = false; }));            // signed out, or not known: no sign-in prompt, no read
    CHECK(!with([](Facts& f) { f.modelsKnown = true; }));          // a session, the cache or an earlier read said them
    CHECK(!with([](Facts& f) { f.readThisLaunch = true; }));       // once per launch
    CHECK(!with([](Facts& f) { f.sessionOfThisAgent = true; }));   // never while a chat session of that agent starts or runs
    CHECK(!with([](Facts& f) { f.reading = true; }));              // one at a time
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

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: model_probe_test <fake-acp-agent.exe> <scratch folder>\n"); return 2; }
    g_agent = FullPath(argv[1]); g_tmp = FullPath(argv[2]);
    CreateDirectoryW(g_tmp.c_str(), nullptr);
    if (!Exists(g_agent) || !Exists(g_tmp)) { std::printf("missing agent or folder\n"); return 2; }
    TestDecision(); TestRead(); TestNotSignedIn(); TestFailures();
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAILED" : "model_probe_test passed", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
