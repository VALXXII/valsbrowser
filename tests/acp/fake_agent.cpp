// Fake ACP v1 agent for tests (never a real model, never the network). Speaks JSON-RPC 2.0, one message per line,
// on stdin/stdout like the real adapters (claude-agent-acp, codex-acp). Used by tests/acp/acp_client_test.cpp, by the
// runtime tests, and by the browser's sandbox (BROWSER_CHAT_FAKE_AGENT, see design/chat-acp.md).
//
// Options (command line):
//   --log <file>          append every received line (cut at 2000 bytes) and a few markers ("child <pid>", "cwd <dir>",
//                         "prompt-text <the last text block of a session/prompt, on one line>")
//   --auth-file <file>    signed in only when the file exists: session/new answers -32000 until `authenticate`
//                         (method "fake-login") or the terminal sign-in (`--cli login`) creates it
//   --cli login           act as the agent's CLI sign-in: create --auth-file (or %FAKE_AGENT_AUTH_FILE%), exit 0
//   --crash-at <step>     exit 3 with a message on stderr at: initialize | new | prompt
//   --silent              never answer initialize
//   --protocol <n>        answer initialize with this protocol version
//   --no-images           promptCapabilities.image = false
//   --resume              advertise session/resume and session/load; only the session "sess-known" can be resumed
//   --garbage             print lines that are not JSON-RPC around the answers
//   --child               on session/new start a long-lived child process (Job Object test)
//   --read-delay <ms>     after answering session/new, do not read stdin for this long (slow reader)
//   --env-dump <file>     write a few environment variables of this process
//   --edit <spec>         the edit of the "apply" scenario: "<path relative to the session folder>|<find>|<replace>"
//                         (also %FAKE_AGENT_EDIT%). With it, a message that names no other scenario applies the edit.
//   --claude-like         the real Claude adapter's option lists: effort "default" (current) + low / medium / high / xhigh / max,
//                         a "default" model alias ("Default (recommended)", description "Sonnet 5.5") and a 4th model
//   --sleep               (internal) the long-lived child
//
// Scenarios, chosen by a word in the prompt's text:
//   (default) / reply   answer in chunks, usage_update, end_turn        think        thought chunks, then the answer
//   tool                tool_call + updates with locations and a diff   permission   session/request_permission
//   plan                plan, then the plan with new statuses           mode         current_mode_update to "plan"
//   slow                a chunk every 100 ms until session/cancel       crash        exit 7 in the middle of the turn
//   question            elicitation/create (two single-choice fields)   refusal / max_tokens   that stop reason
//   error-rate / error-auth   JSON-RPC error                            big          "received <n> image bytes"
//   echo                the text back                                   garbage      invalid lines inside the turn
//   commands            available_commands_update                       title        session_info_update
//   mcptool             an MCP tool asked for by its raw name: "mcptool <title>|<arguments JSON>[|<result text>]" in the message
//                       several calls in one turn: specs separated by ";;"; a part "wait <ms>" pauses between two calls
//   annotations         a message naming browser_annotation_claim (the browser's « Traiter les annotations » instruction): for
//                       each line "- ann_<id> : "<note>"", get, claim, a pause, resolve with a summary; « impossible » in the
//                       note -> release with a reason; « oublie » in the note -> no call at all
//                       (title as the real adapters write it: "mcp__<server>__<tool>" for Claude Code, "mcp.<server>.<tool>" for
//                       Codex); tool_call kind other with rawInput, session/request_permission (allow / always / reject), result
//   apply               REALLY replaces a text in a file of the session folder (tool_call kind edit, locations, diff):
//                       asks permission in mode "default", writes at once in "acceptEdits", only plans in "plan";
//                       refuses a path outside the session folder. "apply <path>|<find>|<replace>" in the message, or --edit.
// The scenario word is looked for in the LAST text block of the prompt only (the user's message), so a preamble or
// an element description placed before it never triggers one.
#include <windows.h>

#include <condition_variable>
#include <functional>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../src/strict-json.h"

namespace {
namespace SJ = StrictJson;

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
bool Exists(const std::wstring& p) { return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
void Append(const std::wstring& path, const std::string& text) {
    if (path.empty()) return;
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD put = 0;
    WriteFile(f, text.data(), DWORD(text.size()), &put, nullptr);
    CloseHandle(f);
}

struct Options {
    std::wstring log, authFile, envDump;
    std::string edit;  // --edit / FAKE_AGENT_EDIT: "path|find|replace"
    std::string crashAt;
    bool silent = false, noImages = false, resume = false, garbage = false, child = false;
    bool claudeLike = false;  // --claude-like: the real Claude adapter's lists ("Default" effort first and current, xhigh, max; a "default" model alias)
    int protocol = 1;
    unsigned readDelay = 0;
} opt;

// ---------------------------------------------------------------- transport
std::mutex outMu;
void Raw(const std::string& bytes) {
    std::lock_guard<std::mutex> lock(outMu);
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    for (std::size_t off = 0; off < bytes.size();) {
        DWORD put = 0;
        if (!WriteFile(out, bytes.data() + off, DWORD(bytes.size() - off), &put, nullptr) || !put) ExitProcess(0);
        off += put;
    }
}
void Send(const SJ::Value& v) { Raw(SJ::Serialize(v) + "\n"); }

std::mutex inMu;
std::condition_variable inCv;
std::deque<std::string> lines;
bool inputClosed = false;
unsigned readDelay = 0;  // set once, consumed by the reader

void ReaderThread() {
    const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    std::string buffer;
    char chunk[65536];
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(inMu);
            if (readDelay) {
                const unsigned ms = readDelay;
                readDelay = 0;
                lock.unlock();
                Sleep(ms);
            }
        }
        DWORD got = 0;
        if (!ReadFile(in, chunk, sizeof chunk, &got, nullptr) || !got) break;
        buffer.append(chunk, got);
        std::size_t start = 0;
        for (std::size_t nl; (nl = buffer.find('\n', start)) != std::string::npos; start = nl + 1) {
            std::string line = buffer.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            Append(opt.log, line.substr(0, 2000) + "\n");
            std::lock_guard<std::mutex> lock(inMu);
            lines.push_back(std::move(line));
            inCv.notify_all();
        }
        buffer.erase(0, start);
    }
    std::lock_guard<std::mutex> lock(inMu);
    inputClosed = true;
    inCv.notify_all();
}
// Next line, or false after `timeoutMs` (0 = wait for ever). Exits the process when stdin is closed.
bool NextLine(std::string& out, unsigned timeoutMs) {
    std::unique_lock<std::mutex> lock(inMu);
    const auto ready = [] { return !lines.empty() || inputClosed; };
    if (timeoutMs) {
        if (!inCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready)) return false;
    } else {
        inCv.wait(lock, ready);
    }
    if (lines.empty()) ExitProcess(0);  // the client closed our stdin: a well-behaved agent exits
    out = std::move(lines.front());
    lines.pop_front();
    return true;
}

// ---------------------------------------------------------------- JSON helpers
SJ::Value Obj(std::initializer_list<std::pair<const char*, SJ::Value>> members) {
    SJ::Value v = SJ::Value::Obj();
    for (const auto& m : members) v.Set(m.first, m.second);
    return v;
}
SJ::Value Arr(std::initializer_list<SJ::Value> items) {
    SJ::Value v = SJ::Value::Arr();
    for (const auto& i : items) v.Push(i);
    return v;
}
SJ::Value S(const std::string& s) { return SJ::Value::Str(s); }
SJ::Value I(std::int64_t i) { return SJ::Value::Int(i); }
SJ::Value B(bool b) { return SJ::Value::Bool(b); }
std::string Str(const SJ::Value* v, const char* key) {
    const SJ::Value* m = v ? v->Get(key) : nullptr;
    return m && m->IsString() ? m->string : std::string();
}

void Result(const SJ::Value& id, SJ::Value result) { Send(Obj({{"jsonrpc", S("2.0")}, {"id", id}, {"result", std::move(result)}})); }
void Fail(const SJ::Value& id, int code, const std::string& message, SJ::Value data = SJ::Value()) {
    SJ::Value err = Obj({{"code", I(code)}, {"message", S(message)}});
    if (!data.IsNull()) err.Set("data", std::move(data));
    Send(Obj({{"jsonrpc", S("2.0")}, {"id", id}, {"error", std::move(err)}}));
}

// ---------------------------------------------------------------- session state
std::string sessionId, sessionCwd;
std::string mcpServer;   // the first MCP server the client handed to the session (its name titles the « annotations » scenario's calls)
std::string mode = "default", model = "sonnet", effort = "medium";
bool cancelled = false;
std::int64_t nextRequest = 1000;

void Update(SJ::Value update) {
    Send(Obj({{"jsonrpc", S("2.0")}, {"method", S("session/update")},
              {"params", Obj({{"sessionId", S(sessionId)}, {"update", std::move(update)}})}}));
}
void Chunk(const char* kind, const std::string& text) {
    Update(Obj({{"sessionUpdate", S(kind)}, {"content", Obj({{"type", S("text")}, {"text", S(text)}})}, {"messageId", S("m1")}}));
}
SJ::Value Modes() {
    if (opt.claudeLike)   // claude-agent-acp 0.107: English names, a 4th mode « auto » (bypassPermissions is never shown by the browser)
        return Obj({{"currentModeId", S(mode)},
                    {"availableModes", Arr({Obj({{"id", S("default")}, {"name", S("Manual")}, {"description", S("Always ask before making changes")}}),
                                            Obj({{"id", S("acceptEdits")}, {"name", S("Accept edits")}, {"description", S("Automatically accept all file edits")}}),
                                            Obj({{"id", S("plan")}, {"name", S("Plan")}, {"description", S("Create a plan before making changes")}}),
                                            Obj({{"id", S("auto")}, {"name", S("Auto")}, {"description", S("Claude handles permission decisions")}})})}});
    return Obj({{"currentModeId", S(mode)},
                {"availableModes", Arr({Obj({{"id", S("default")}, {"name", S("Demander")}, {"description", S("Demande avant chaque modification")}}),
                                        Obj({{"id", S("acceptEdits")}, {"name", S("Auto")}}),
                                        Obj({{"id", S("plan")}, {"name", S("Planifier")}})})}});
}
SJ::Value ConfigOptions() {
    SJ::Value efforts = Arr({Obj({{"value", S("low")}, {"name", S("Faible")}}), Obj({{"value", S("medium")}, {"name", S("Moyen")}})});
    if (model != "haiku") efforts.Push(Obj({{"value", S("high")}, {"name", S("Élevé")}}));  // Haiku has no high effort
    if (opt.claudeLike) {   // as claude-agent-acp 0.107 says it (build/acp-test/real-claude-as-user.json)
        efforts = Arr({Obj({{"value", S("default")}, {"name", S("Default")}}), Obj({{"value", S("low")}, {"name", S("Low")}}), Obj({{"value", S("medium")}, {"name", S("Medium")}})});
        if (model != "haiku") { efforts.Push(Obj({{"value", S("high")}, {"name", S("High")}})); efforts.Push(Obj({{"value", S("xhigh")}, {"name", S("Xhigh")}})); efforts.Push(Obj({{"value", S("max")}, {"name", S("Max")}})); }
    }
    SJ::Value alias = Obj({{"value", S("default")}, {"name", S("Default (recommended)")}, {"description", S("Sonnet 5.5")}});
    return Arr({
        Obj({{"id", S("model")}, {"name", S("Modèle")}, {"category", S("model")}, {"type", S("select")}, {"currentValue", S(model)},
             {"options", Arr({Obj({{"group", S("claude")}, {"name", S("Claude")},
                                   {"options", opt.claudeLike ? Arr({std::move(alias), Obj({{"value", S("opus")}, {"name", S("Opus 5.5")}, {"description", S("Le plus capable")}}),
                                                    Obj({{"value", S("sonnet")}, {"name", S("Sonnet 5.5")}}),
                                                    Obj({{"value", S("haiku")}, {"name", S("Haiku 5.5")}}), Obj({{"value", S("sonnet-4")}, {"name", S("Sonnet 4.6")}})}) :
                                                    Arr({Obj({{"value", S("opus")}, {"name", S("Opus 5.5")}, {"description", S("Le plus capable")}}),
                                                    Obj({{"value", S("sonnet")}, {"name", S("Sonnet 5.5")}}),
                                                    Obj({{"value", S("haiku")}, {"name", S("Haiku 5.5")}})})}})})}}),
        Obj({{"id", S("effort")}, {"name", S("Effort")}, {"category", S("thought_level")}, {"type", S("select")}, {"currentValue", S(effort)},
             {"options", std::move(efforts)}}),
        Obj({{"id", S("mode")}, {"name", S("Mode")}, {"category", S("mode")}, {"type", S("select")}, {"currentValue", S(mode)},
             {"options", Arr({Obj({{"value", S("default")}, {"name", S("Demander")}}), Obj({{"value", S("acceptEdits")}, {"name", S("Auto")}}),
                              Obj({{"value", S("plan")}, {"name", S("Planifier")}})})}}),
        Obj({{"id", S("fast")}, {"name", S("Rapide")}, {"category", S("model_config")}, {"type", S("boolean")}, {"currentValue", B(false)}}),
    });
}
bool SignedIn() { return opt.authFile.empty() || Exists(opt.authFile); }

// A JSON-RPC message handled outside a turn's own logic. Returns the response to request `waitFor` when it comes.
bool Dispatch(const SJ::Value& msg, std::int64_t waitFor, SJ::Value* response);

// Handles incoming messages for `ms`; true when the response to `waitFor` arrived.
bool Pump(unsigned ms, std::int64_t waitFor = -1, SJ::Value* response = nullptr) {
    const DWORD until = GetTickCount() + ms;
    for (;;) {
        const DWORD now = GetTickCount();
        const unsigned left = int(until - now) > 0 ? unsigned(until - now) : 0;
        std::string line;
        if (!NextLine(line, left ? left : 1)) return false;
        SJ::Value msg;
        SJ::Limits limits;
        limits.maxBytes = 64u << 20;
        limits.maxStringBytes = 64u << 20;
        if (!SJ::Parse(line, msg, nullptr, limits) || !msg.IsObject()) continue;
        if (Dispatch(msg, waitFor, response)) return true;
        if (int(until - GetTickCount()) <= 0 && waitFor < 0) return false;
    }
}

void StartChild() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" --sleep";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        Append(opt.log, "child " + std::to_string(pi.dwProcessId) + "\n");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void Crash(int code, const char* why) {
    const std::string text = std::string("fake-agent: ") + why + "\n";
    DWORD put = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text.data(), DWORD(text.size()), &put, nullptr);
    ExitProcess(UINT(code));
}

// ---------------------------------------------------------------- a real edit in the project folder
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
// The absolute path of `relative` inside the session folder, or "" when it would leave it.
std::wstring InsideProject(const std::string& relative) {
    const std::wstring rel = Widen(relative);
    if (rel.empty() || sessionCwd.empty() || rel[0] == L'\\' || rel[0] == L'/' || rel.find(L':') != std::wstring::npos) return {};
    wchar_t full[4096], root[4096];
    const DWORD n = GetFullPathNameW((Widen(sessionCwd) + L"\\" + rel).c_str(), 4096, full, nullptr);
    const DWORD r = GetFullPathNameW(Widen(sessionCwd).c_str(), 4096, root, nullptr);
    if (!n || n >= 4096 || !r || r >= 4096) return {};
    std::wstring base(root, r);
    if (base.back() != L'\\') base += L'\\';
    if (n <= base.size() || _wcsnicmp(full, base.c_str(), base.size()) != 0) return {};
    return std::wstring(full, n);
}
bool ReadWhole(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char b[65536];
    DWORD got = 0;
    out.clear();
    while (ReadFile(f, b, sizeof b, &got, nullptr) && got) out.append(b, got);
    CloseHandle(f);
    return true;
}
bool WriteWhole(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(f, data.data(), DWORD(data.size()), &put, nullptr) && put == data.size();
    CloseHandle(f);
    return ok;
}
// "apply": really replaces a text in a file of the session folder, like an agent's Edit tool. `spec` is
// "<path relative to the session folder>|<text to find>|<replacement>". In the mode "default" the edit waits for a
// permission (with the diff); in "acceptEdits" it is done at once; in "plan" nothing is written.
void Apply(const std::string& spec, const std::function<void(const char*)>& end) {
    const std::size_t bar1 = spec.find('|'), bar2 = bar1 == std::string::npos ? bar1 : spec.find('|', bar1 + 1);
    if (bar2 == std::string::npos) {
        Chunk("agent_message_chunk", "apply: no edit configured (FAKE_AGENT_EDIT=path|find|replace)");
        return end("end_turn");
    }
    const std::string relative = spec.substr(0, bar1), find = spec.substr(bar1 + 1, bar2 - bar1 - 1), replacement = spec.substr(bar2 + 1);
    const std::wstring path = InsideProject(relative);
    const std::string shown = path.empty() ? relative : Narrow(path);
    const auto call = [&](const char* status, SJ::Value content, std::int64_t line) {
        SJ::Value location = Obj({{"path", S(shown)}});
        if (line > 0) location.Set("line", I(line));
        return Obj({{"toolCallId", S("edit-" + std::to_string(nextRequest))}, {"title", S("Modifier " + relative)}, {"kind", S("edit")}, {"status", S(status)},
                    {"content", std::move(content)}, {"locations", Arr({std::move(location)})},
                    {"rawInput", Obj({{"file_path", S(shown)}, {"old_string", S(find)}, {"new_string", S(replacement)}})}});
    };
    const auto refuse = [&](const std::string& why) {
        SJ::Value update = call("failed", Arr({Obj({{"type", S("content")}, {"content", Obj({{"type", S("text")}, {"text", S(why)}})}})}), 0);
        update.Set("sessionUpdate", S("tool_call"));
        Update(std::move(update));
        Chunk("agent_message_chunk", "apply: " + why);
        end("end_turn");
    };
    std::string before;
    if (path.empty()) return refuse("refused, the path leaves the project folder");
    if (!ReadWhole(path, before)) return refuse("cannot read " + relative);
    const std::size_t at = find.empty() ? std::string::npos : before.find(find);
    if (at == std::string::npos) return refuse("text not found in " + relative);
    const std::string after = before.substr(0, at) + replacement + before.substr(at + find.size());
    std::int64_t line = 1;
    for (std::size_t i = 0; i < at; ++i) line += before[i] == '\n';
    const SJ::Value diff = Obj({{"type", S("diff")}, {"path", S(shown)}, {"oldText", S(before)}, {"newText", S(after)}});
    if (mode == "plan") {
        Update(Obj({{"sessionUpdate", S("plan")},
                    {"entries", Arr({Obj({{"content", S("Remplacer « " + find + " » par « " + replacement + " » dans " + relative)}, {"priority", S("medium")},
                                          {"status", S("pending")}})})}}));
        Chunk("agent_message_chunk", "Plan prêt, rien n'a été modifié.");
        return end("end_turn");
    }
    SJ::Value started = call("pending", Arr({diff}), line);
    const std::string callId = started.Get("toolCallId")->string;
    started.Set("sessionUpdate", S("tool_call"));
    Update(std::move(started));
    const auto update = [&](const char* status) {
        Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S(callId)}, {"status", S(status)}}));
    };
    if (mode == "default") {
        const std::int64_t request = nextRequest++;
        SJ::Value asked = call("pending", Arr({diff}), line);
        asked.Set("toolCallId", S(callId));
        Send(Obj({{"jsonrpc", S("2.0")}, {"id", I(request)}, {"method", S("session/request_permission")},
                  {"params", Obj({{"sessionId", S(sessionId)}, {"toolCall", std::move(asked)},
                                  {"options", Arr({Obj({{"optionId", S("allow")}, {"name", S("Autoriser")}, {"kind", S("allow_once")}}),
                                                   Obj({{"optionId", S("always")}, {"name", S("Toujours autoriser")}, {"kind", S("allow_always")}}),
                                                   Obj({{"optionId", S("reject")}, {"name", S("Refuser")}, {"kind", S("reject_once")}})})}})}}));
        SJ::Value response;
        while (!Pump(1000, request, &response)) {}
        const SJ::Value* outcome = response.Get("outcome");
        const std::string option = Str(outcome, "optionId");
        if (Str(outcome, "outcome") != "selected") {
            update("failed");
            Pump(300);
            cancelled = true;
            return end("cancelled");
        }
        if (option == "reject") {
            update("failed");
            Chunk("agent_message_chunk", "D'accord, je n'ai rien modifié.");
            return end("end_turn");
        }
        if (option == "always") mode = "acceptEdits";  // like "always allow": the next edits do not ask
    } else {
        ++nextRequest;
    }
    update("in_progress");
    Pump(20);
    if (!WriteWhole(path, after)) {
        update("failed");
        Chunk("agent_message_chunk", "apply: cannot write " + relative);
        return end("end_turn");
    }
    Append(opt.log, "applied " + relative + "\n");
    Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S(callId)}, {"status", S("completed")}, {"title", S("Modifié " + relative)},
                {"content", Arr({diff})}, {"locations", Arr({Obj({{"path", S(shown)}, {"line", I(line)}})})}}));
    Chunk("agent_message_chunk", "C'est fait : « " + find + " » remplacé par « " + replacement + " » dans " + relative + ".");
    Update(Obj({{"sessionUpdate", S("usage_update")}, {"used", I(2345)}, {"size", I(200000)}}));
    end("end_turn");
}

// ---------------------------------------------------------------- the turn
void Prompt(const SJ::Value& id, const SJ::Value* params) {
    if (opt.crashAt == "prompt") Crash(3, "crash at prompt");
    std::string text, all;
    std::size_t imageBytes = 0, links = 0;
    if (const SJ::Value* blocks = params ? params->Get("prompt") : nullptr)
        for (const SJ::Value& b : blocks->items) {
            const std::string type = Str(&b, "type");
            if (type == "text") {
                text = Str(&b, "text") + "\n";  // scenarios are chosen in the LAST text block: the user's own message
                all += text;
            }
            else if (type == "image") imageBytes += Str(&b, "data").size();
            else if (type == "resource_link") ++links;
        }
    cancelled = false;
    {   // the user's own message as received (the last text block, one line): what the tests read back (V1.2: the note's text)
        std::string one = text; for (auto& c : one) if (c == '\n' || c == '\r') c = ' ';
        Append(opt.log, "prompt-text " + one.substr(0, 2000) + "\n");
    }
    const auto has = [&](const char* word) { return text.find(word) != std::string::npos; };
    const auto end = [&](const char* reason) { Result(id, Obj({{"stopReason", S(cancelled ? "cancelled" : reason)}})); };

    const bool batch = !has("mcptool ") && has("browser_annotation_claim");
    if (has("mcptool ") || batch) {   // first: its title, arguments and result are free text and must not pick another scenario
        // "mcptool <title>|<arguments as JSON>[|<result text>]": an MCP tool asked for by its raw name, as the real adapters
        // title it (Claude Code "mcp__<server>__<tool>", Codex "mcp.<server>.<tool>"), with its arguments in rawInput.
        // Several calls in one turn: specs separated by ";;" (a part "wait <ms>" pauses between two calls).
        std::string all;
        if (!batch) { const std::size_t at = text.find("mcptool "); all = text.substr(at + 8, text.find('\n', at) - at - 8); }
        else {
            // « annotations »: a message that names browser_annotation_claim is the browser's batch instruction (« Traiter les
            // annotations »). Like a well-mannered agent, for each line "- ann_<id> : "<note>"": get, claim, a pause, then
            // resolve with a one-sentence summary — or release with a reason when the note says « impossible »; a note that
            // says « oublie » is skipped altogether (the browser then says what was left). The calls go to no real server:
            // the client sees them exactly as a real adapter would report them.
            const std::string server = mcpServer.empty() ? std::string("browser-annotations") : mcpServer;
            const auto tool = [&](const char* name) { return "mcp__" + server + "__" + name; };
            for (std::size_t line = 0; line < text.size();) {
                std::size_t eol = text.find('\n', line); if (eol == std::string::npos) eol = text.size();
                const std::string row = text.substr(line, eol - line); line = eol + 1;
                if (row.rfind("- ann_", 0) != 0) continue;
                const std::string annotation = row.substr(2, row.find(' ', 2) == std::string::npos ? std::string::npos : row.find(' ', 2) - 2);
                if (row.find("oublie") != std::string::npos) continue;
                const std::string idArg = "{\"id\":\"" + annotation + "\"";
                all += tool("browser_annotation_get") + "|" + idArg + "}|" + idArg + "};;";
                all += tool("browser_annotation_claim") + "|" + idArg + "}|" + idArg + ",\"claim_token\":\"tok\"};;wait 1200;;";
                if (row.find("impossible") != std::string::npos) all += tool("browser_annotation_release") + "|" + idArg + ",\"claim_token\":\"tok\",\"reason\":\"Je n'ai pas trouvé cet élément dans le code.\"};;";
                else all += tool("browser_annotation_resolve") + "|" + idArg + ",\"claim_token\":\"tok\",\"outcome\":\"resolved\",\"summary\":\"C'est corrigé dans le code (faux agent).\"};;";
            }
        }
        std::string option;
        for (std::size_t from = 0; from <= all.size();) {
            const std::size_t sep = all.find(";;", from);
            const std::string spec = all.substr(from, sep == std::string::npos ? std::string::npos : sep - from);
            from = sep == std::string::npos ? all.size() + 1 : sep + 2;
            if (spec.empty()) continue;
            if (spec.rfind("wait ", 0) == 0) { Pump(std::min(10000, std::max(0, atoi(spec.c_str() + 5)))); continue; }
            const std::size_t bar1 = spec.find('|'), bar2 = bar1 == std::string::npos ? bar1 : spec.find('|', bar1 + 1);
            const std::string title = spec.substr(0, bar1);
            const std::string argsText = bar1 == std::string::npos ? "{}" : spec.substr(bar1 + 1, bar2 == std::string::npos ? std::string::npos : bar2 - bar1 - 1);
            const std::string result = bar2 == std::string::npos ? "{\"ok\":true}" : spec.substr(bar2 + 1);
            SJ::Value args;
            if (!SJ::Parse(argsText, args)) args = SJ::Value::Obj();
            const std::string callId = "mcp-" + std::to_string(nextRequest);
            const auto call = [&](const char* status) {
                return Obj({{"toolCallId", S(callId)}, {"title", S(title)}, {"kind", S("other")}, {"status", S(status)}, {"rawInput", args}});
            };
            SJ::Value started = call("pending");
            started.Set("sessionUpdate", S("tool_call"));
            Update(std::move(started));
            const std::int64_t request = nextRequest++;
            Send(Obj({{"jsonrpc", S("2.0")}, {"id", I(request)}, {"method", S("session/request_permission")},
                      {"params", Obj({{"sessionId", S(sessionId)}, {"toolCall", call("pending")},
                                      {"options", Arr({Obj({{"optionId", S("allow")}, {"name", S("Autoriser")}, {"kind", S("allow_once")}}),
                                                       Obj({{"optionId", S("always")}, {"name", S("Toujours autoriser")}, {"kind", S("allow_always")}}),
                                                       Obj({{"optionId", S("reject")}, {"name", S("Refuser")}, {"kind", S("reject_once")}})})}})}}));
            SJ::Value response;
            while (!Pump(1000, request, &response)) {}
            const SJ::Value* outcome = response.Get("outcome");
            option = Str(outcome, "optionId");
            Append(opt.log, "mcptool-outcome " + Str(outcome, "outcome") + " " + option + "\n");
            if (Str(outcome, "outcome") != "selected") {
                Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S(callId)}, {"status", S("failed")}}));
                Pump(300);
                cancelled = true;
                return end("cancelled");
            }
            if (option == "reject") {
                Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S(callId)}, {"status", S("failed")}}));
                Chunk("agent_message_chunk", "refused");
                return end("end_turn");
            }
            Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S(callId)}, {"status", S("in_progress")}}));
            Pump(20);
            // as the real adapters do: the last update carries the status and the result, not the arguments again
            Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S(callId)}, {"status", S("completed")},
                        {"content", Arr({Obj({{"type", S("content")}, {"content", Obj({{"type", S("text")}, {"text", S(result)}})}})})}}));
            if (cancelled) return end("cancelled");
        }
        Chunk("agent_message_chunk", batch ? std::string("J'ai traité les annotations que j'ai pu traiter.") : "mcptool:" + option);
        return end("end_turn");
    }
    if (has("error-rate")) {
        Fail(id, -32603, "Internal error", Obj({{"details", S("API Error: 429 {\"type\":\"error\",\"error\":{\"type\":\"rate_limit_error\"}}")}}));
        return;
    }
    if (has("error-auth")) {
        Fail(id, -32000, "Authentication required");
        return;
    }
    if (has("refusal")) return end("refusal");
    if (has("max_tokens")) return end("max_tokens");
    if (has("echo")) {
        Chunk("agent_message_chunk", all);
        return end("end_turn");
    }
    if (has("big")) {
        Chunk("agent_message_chunk", "received " + std::to_string(imageBytes) + " image bytes, " + std::to_string(links) + " links");
        return end("end_turn");
    }
    if (has("think")) {
        Chunk("agent_thought_chunk", "Je regarde le composant ");
        Pump(20);
        Chunk("agent_thought_chunk", "et ses styles.");
    }
    if (has("commands"))
        Update(Obj({{"sessionUpdate", S("available_commands_update")},
                    {"availableCommands", Arr({Obj({{"name", S("review")}, {"description", S("Relire les changements")},
                                                    {"input", Obj({{"hint", S("fichier")}})}})})}}));
    if (has("title")) Update(Obj({{"sessionUpdate", S("session_info_update")}, {"title", S("Bouton du héros")}}));
    if (has("garbage")) {
        Raw("this is not json\n");
        Raw("{\"jsonrpc\":\"2.0\",\"method\":\n");
        Raw("[1,2,3]\n");
        Raw("{\"dup\":1,\"dup\":2}\n");
    }
    if (has("plan")) {
        const auto entry = [](const char* content, const char* status) {
            return Obj({{"content", S(content)}, {"priority", S("medium")}, {"status", S(status)}});
        };
        Update(Obj({{"sessionUpdate", S("plan")},
                    {"entries", Arr({entry("Lire Hero.tsx", "in_progress"), entry("Changer le padding", "pending"), entry("Vérifier", "pending")})}}));
        Pump(20);
        Update(Obj({{"sessionUpdate", S("plan")},
                    {"entries", Arr({entry("Lire Hero.tsx", "completed"), entry("Changer le padding", "in_progress"), entry("Vérifier", "pending")})}}));
    }
    if (has("mode")) {
        mode = "plan";
        Update(Obj({{"sessionUpdate", S("current_mode_update")}, {"currentModeId", S(mode)}}));
    }
    if (has("apply")) {
        // "apply <path>|<find>|<replace>" in the message, else the configured edit.
        const std::size_t at = text.find("apply ");
        std::string spec = at == std::string::npos ? std::string() : text.substr(at + 6, text.find('\n', at) - at - 6);
        if (spec.find('|') == std::string::npos) spec = opt.edit;
        return Apply(spec, end);
    }
    if (has("tool") || has("permission")) {
        const SJ::Value diff = Obj({{"type", S("diff")}, {"path", S("C:\\projet\\src\\Hero.tsx")},
                                    {"oldText", S("<button className=\"p-2\">\n")}, {"newText", S("<button className=\"p-4\">\n")}});
        Update(Obj({{"sessionUpdate", S("tool_call")}, {"toolCallId", S("call-1")}, {"title", S("Modifier src/Hero.tsx")}, {"kind", S("edit")},
                    {"status", S("pending")}, {"locations", Arr({Obj({{"path", S("C:\\projet\\src\\Hero.tsx")}, {"line", I(12)}})})},
                    {"rawInput", Obj({{"file_path", S("C:\\projet\\src\\Hero.tsx")}})}}));
        if (has("permission")) {
            const std::int64_t request = nextRequest++;
            Send(Obj({{"jsonrpc", S("2.0")}, {"id", I(request)}, {"method", S("session/request_permission")},
                      {"params", Obj({{"sessionId", S(sessionId)},
                                      {"toolCall", Obj({{"toolCallId", S("call-1")}, {"title", S("Modifier src/Hero.tsx")}, {"kind", S("edit")},
                                                        {"content", Arr({diff})},
                                                        {"locations", Arr({Obj({{"path", S("C:\\projet\\src\\Hero.tsx")}})})}})},
                                      {"options", Arr({Obj({{"optionId", S("allow")}, {"name", S("Autoriser")}, {"kind", S("allow_once")}}),
                                                       Obj({{"optionId", S("always")}, {"name", S("Toujours autoriser")}, {"kind", S("allow_always")}}),
                                                       Obj({{"optionId", S("reject")}, {"name", S("Refuser")}, {"kind", S("reject_once")}})})}})}}));
            SJ::Value response;
            while (!Pump(1000, request, &response)) {}
            const SJ::Value* outcome = response.Get("outcome");
            const std::string what = Str(outcome, "outcome");
            const std::string option = Str(outcome, "optionId");
            Append(opt.log, "permission-outcome " + what + " " + option + "\n");
            if (what != "selected") {
                Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S("call-1")}, {"status", S("failed")}}));
                Pump(300);  // the cancel notification follows the cancelled outcome
                cancelled = true;
                return end("cancelled");
            }
            if (option == "reject") {
                Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S("call-1")}, {"status", S("failed")}}));
                Chunk("agent_message_chunk", "refused");
                return end("end_turn");
            }
            Chunk("agent_message_chunk", "ok:" + option + " ");
        }
        Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S("call-1")}, {"status", S("in_progress")}}));
        Pump(20);
        Update(Obj({{"sessionUpdate", S("tool_call_update")}, {"toolCallId", S("call-1")}, {"status", S("completed")},
                    {"title", S("Modifié src/Hero.tsx")},
                    {"content", Arr({diff, Obj({{"type", S("content")}, {"content", Obj({{"type", S("text")}, {"text", S("1 ligne modifiée")}})}})})}}));
    }
    if (has("question")) {
        const std::int64_t request = nextRequest++;
        const auto choice = [](const char* value, const char* title, const char* description) {
            return Obj({{"const", S(value)}, {"title", S(title)}, {"description", S(description)}});
        };
        Send(Obj({{"jsonrpc", S("2.0")}, {"id", I(request)}, {"method", S("elicitation/create")},
                  {"params", Obj({{"sessionId", S(sessionId)}, {"mode", S("form")}, {"message", S("Deux questions avant de continuer")},
                                  {"requestedSchema",
                                   Obj({{"type", S("object")},
                                        {"properties",
                                         Obj({{"size", Obj({{"type", S("string")}, {"title", S("Taille")}, {"description", S("Quelle taille pour le bouton ?")},
                                                            {"oneOf", Arr({choice("s", "Petit", "32 px"), choice("l", "Grand", "48 px")})}})},
                                              {"note", Obj({{"type", S("string")}, {"title", S("Remarque")}})},
                                              {"tone", Obj({{"type", S("string")}, {"title", S("Quelle couleur ?")},
                                                            {"enum", Arr({S("bleu"), S("vert")})}})}})},
                                        {"required", Arr({S("size"), S("tone")})}})}})}}));
        SJ::Value response;
        while (!Pump(1000, request, &response)) {}
        const std::string action = Str(&response, "action");
        const SJ::Value* content = response.Get("content");
        Chunk("agent_message_chunk", "question:" + action + ":" + Str(content, "size") + ":" + Str(content, "tone"));
        if (action == "cancel") {
            Pump(300);
            cancelled = true;
            return end("cancelled");
        }
        return end("end_turn");
    }
    if (has("crash")) {
        Chunk("agent_message_chunk", "Je commence");
        Pump(50);
        Crash(7, "boom: the model backend went away");
    }
    if (has("slow")) {
        for (int i = 0; i < 300 && !cancelled; ++i) {
            Chunk("agent_message_chunk", "… ");
            Pump(100);
        }
        return end("end_turn");
    }
    // With an edit configured, a message that asks for nothing else is a request to apply it.
    if (!opt.edit.empty() && !has("reply") && !has("think") && !has("plan") && !has("mode") && !has("commands") && !has("title") && !has("garbage"))
        return Apply(opt.edit, end);
    for (const char* part : {"Bonjour", " — voici", " la réponse."}) {
        Chunk("agent_message_chunk", part);
        Pump(15);
    }
    Update(Obj({{"sessionUpdate", S("usage_update")}, {"used", I(1234)}, {"size", I(200000)},
                {"cost", Obj({{"amount", SJ::Value::Num(0.0125)}, {"currency", S("USD")}})}}));
    end("end_turn");
}

bool Dispatch(const SJ::Value& msg, std::int64_t waitFor, SJ::Value* response) {
    const SJ::Value* method = msg.Get("method");
    const SJ::Value* id = msg.Get("id");
    const SJ::Value* params = msg.Get("params");
    if (!method) {  // a response to one of our requests
        if (id && id->IsNumber() && id->integer == waitFor && response) {
            const SJ::Value* result = msg.Get("result");
            *response = result ? *result : SJ::Value::Obj();
            return true;
        }
        return false;
    }
    const std::string m = method->string;
    if (m == "session/cancel") {
        cancelled = true;
        return false;
    }
    if (!id) return false;
    if (m == "initialize") {
        if (opt.crashAt == "initialize") Crash(3, "crash at initialize: cannot find the CLI");
        if (opt.silent) return false;
        if (opt.garbage) Raw("Welcome to the fake agent!\n\n{broken\n");
        SJ::Value session = SJ::Value::Obj();
        if (opt.resume) session.Set("resume", SJ::Value::Obj());
        Result(*id, Obj({{"protocolVersion", I(opt.protocol)},
                         {"agentCapabilities", Obj({{"loadSession", B(opt.resume)},
                                                    {"promptCapabilities", Obj({{"image", B(!opt.noImages)}, {"embeddedContext", B(true)}})},
                                                    {"mcpCapabilities", Obj({{"http", B(true)}, {"sse", B(false)}})},
                                                    {"sessionCapabilities", std::move(session)}})},
                         {"authMethods", Arr({Obj({{"id", S("fake-login")}, {"name", S("Fake account")}, {"description", S("Opens the browser")}}),
                                              Obj({{"id", S("fake-terminal")}, {"name", S("Fake terminal login")}, {"type", S("terminal")},
                                                   {"args", Arr({S("--cli"), S("login")})}, {"env", Obj({{"FAKE_LOGIN", S("1")}})}})})},
                         {"agentInfo", Obj({{"name", S("fake-agent")}, {"title", S("Fake Agent")}, {"version", S("1.2.3")}})}}));
        // Like Claude's adapter: the login the agent itself uses, pushed right after initialize.
        Send(Obj({{"jsonrpc", S("2.0")}, {"method", S("_auth/status_update")},
                  {"params", Obj({{"authStatus", Obj({{"kind", S(SignedIn() ? "account" : "none")}, {"label", S("Fake")},
                                                      {"account", Obj({{"email", S("someone@example.test")}})}})}})}}));
        return false;
    }
    if (m == "authenticate") {
        if (Str(params, "methodId") != "fake-login") {
            Fail(*id, -32602, "Invalid params", Obj({{"details", S("unknown auth method")}}));
            return false;
        }
        Append(opt.authFile, "signed-in\n");
        Result(*id, SJ::Value::Obj());
        return false;
    }
    if (m == "session/new" || m == "session/resume" || m == "session/load") {
        if (opt.crashAt == "new") Crash(3, "crash at session/new");
        if (!SignedIn()) {
            Fail(*id, -32000, "Authentication required", Obj({{"details", S("Please run `fake login` first")}}));
            return false;
        }
        const bool fresh = m == "session/new";
        if (const SJ::Value* servers = params ? params->Get("mcpServers") : nullptr; servers && servers->IsArray() && !servers->items.empty()) mcpServer = Str(&servers->items.front(), "name");
        if (!fresh && Str(params, "sessionId") != "sess-known") {
            Fail(*id, -32002, "Resource not found", Obj({{"details", S("no such session")}}));
            return false;
        }
        if (m == "session/load") {  // a replay, which the client must drop
            sessionId = "sess-known";
            Chunk("agent_message_chunk", "REPLAYED HISTORY");
        }
        sessionId = fresh ? "sess-" + std::to_string(GetCurrentProcessId()) : "sess-known";
        sessionCwd = Str(params, "cwd");
        Append(opt.log, "cwd " + sessionCwd + "\n");
        if (opt.child) StartChild();
        SJ::Value result = Obj({{"modes", Modes()}, {"configOptions", ConfigOptions()}});
        if (fresh) result.Set("sessionId", S(sessionId));
        Result(*id, std::move(result));
        Update(Obj({{"sessionUpdate", S("available_commands_update")},
                    {"availableCommands", Arr({Obj({{"name", S("init")}, {"description", S("Décrire le projet")}})})}}));
        if (opt.readDelay) {
            std::lock_guard<std::mutex> lock(inMu);
            readDelay = opt.readDelay;
        }
        return false;
    }
    if (m == "session/set_mode") {
        const std::string wanted = Str(params, "modeId");
        if (wanted != "default" && wanted != "acceptEdits" && wanted != "plan" && !(opt.claudeLike && wanted == "auto")) {
            Fail(*id, -32602, "Invalid params", Obj({{"details", S("unknown mode " + wanted)}}));
            return false;
        }
        mode = wanted;
        Result(*id, SJ::Value::Obj());
        return false;
    }
    if (m == "session/set_config_option") {
        const std::string config = Str(params, "configId");
        const std::string value = Str(params, "value");
        if (config == "model" && (value == "opus" || value == "sonnet" || value == "haiku" || (opt.claudeLike && (value == "default" || value == "sonnet-4")))) {
            model = value == "default" ? "sonnet" : value;
            if (model == "haiku" && (effort == "high" || effort == "xhigh" || effort == "max")) effort = "medium";
        } else if (config == "effort" && (value == "low" || value == "medium" || (value == "high" && model != "haiku") ||
                                          (opt.claudeLike && (value == "default" || ((value == "xhigh" || value == "max") && model != "haiku"))))) {
            effort = value;
        } else if (config == "mode" && (value == "default" || value == "acceptEdits" || value == "plan" || (opt.claudeLike && value == "auto"))) {
            mode = value;
        } else if (config == "fast") {
            // boolean option: accepted, not stored
        } else {
            Fail(*id, -32602, "Invalid params", Obj({{"details", S("unknown value " + value + " for " + config)}}));
            return false;
        }
        Result(*id, Obj({{"configOptions", ConfigOptions()}}));
        return false;
    }
    if (m == "session/prompt") {
        Prompt(*id, params);
        return false;
    }
    Fail(*id, -32601, "Method not found");
    return false;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    bool cli = false;
    std::wstring cliVerb;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        const auto value = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--sleep") {
            Sleep(INFINITE);
            return 0;
        }
        if (a == L"--log") opt.log = value();
        else if (a == L"--auth-file") opt.authFile = value();
        else if (a == L"--env-dump") opt.envDump = value();
        else if (a == L"--crash-at") opt.crashAt = Narrow(value());
        else if (a == L"--protocol") opt.protocol = _wtoi(value().c_str());
        else if (a == L"--read-delay") opt.readDelay = unsigned(_wtoi(value().c_str()));
        else if (a == L"--silent") opt.silent = true;
        else if (a == L"--no-images") opt.noImages = true;
        else if (a == L"--resume") opt.resume = true;
        else if (a == L"--garbage") opt.garbage = true;
        else if (a == L"--child") opt.child = true;
        else if (a == L"--claude-like") { opt.claudeLike = true; effort = "default"; }
        else if (a == L"--edit") opt.edit = Narrow(value());
        else if (a == L"--cli") cli = true;
        else if (cli && cliVerb.empty()) cliVerb = a;
    }
    if (opt.authFile.empty()) {
        wchar_t buffer[1024];
        const DWORD n = GetEnvironmentVariableW(L"FAKE_AGENT_AUTH_FILE", buffer, 1024);
        if (n && n < 1024) opt.authFile.assign(buffer, n);
    }
    if (opt.edit.empty()) {
        wchar_t buffer[4096];
        const DWORD n = GetEnvironmentVariableW(L"FAKE_AGENT_EDIT", buffer, 4096);
        if (n && n < 4096) opt.edit = Narrow(std::wstring(buffer, n));
    }
    if (cliVerb == L"login" || (argc >= 2 && std::wstring(argv[1]) == L"login")) {  // the CLI's own sign-in
        Append(opt.authFile, "signed-in\n");
        return opt.authFile.empty() ? 2 : 0;
    }
    if (!opt.envDump.empty()) {
        std::string dump;
        for (const wchar_t* name : {L"BROWSER", L"CLAUDE_CODE_EXECUTABLE", L"CODEX_PATH", L"CLAUDE_AGENT_ACP_PROTOCOL", L"FAKE_EXTRA", L"FAKE_REMOVED"}) {
            wchar_t buffer[4096];
            const DWORD n = GetEnvironmentVariableW(name, buffer, 4096);
            dump += Narrow(name) + "=" + (n && n < 4096 ? Narrow(std::wstring(buffer, n)) : std::string("<unset>")) + "\n";
        }
        wchar_t cwd[4096];
        const DWORD n = GetCurrentDirectoryW(4096, cwd);
        dump += "cwd=" + Narrow(std::wstring(cwd, n)) + "\n";
        for (int i = 1; i < argc; ++i) dump += "arg=" + Narrow(argv[i]) + "\n";
        Append(opt.envDump, dump);
    }
    std::thread(ReaderThread).detach();
    for (;;) {
        std::string line;
        NextLine(line, 0);
        SJ::Value msg;
        SJ::Limits limits;
        limits.maxBytes = 64u << 20;
        limits.maxStringBytes = 64u << 20;
        if (!SJ::Parse(line, msg, nullptr, limits) || !msg.IsObject()) continue;
        Dispatch(msg, -1, nullptr);
    }
}
