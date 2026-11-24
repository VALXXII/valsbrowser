// ACP v1 client: see acp-client.h and design/chat-acp.md.
//
// Portions adapted from Gui (Apache-2.0, https://github.com/brijr/gui, commit 0af5e38), rewritten in C++ and
// modified: src/main/agent.ts (session start order, isAuthError wording list, parseModelSettings) and
// src/main/agent-protocol.ts (errorDetail, the known turn failures of plainAgentError, formQuestions /
// questionRequest, resumeMethods). The regular expressions became hand-written matchers and the messages became
// error codes. Licence and notice: third_party/gui/LICENSE, third_party/gui/NOTICE.
#include "acp-client.h"

#include <windows.h>
#include <userenv.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "strict-json.h"

namespace Acp {
namespace {

namespace SJ = StrictJson;
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- text
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
// Cuts at `max` bytes without splitting a UTF-8 sequence.
std::string Clip(std::string s, std::size_t max, bool* cut = nullptr) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    s.resize(n);
    if (cut) *cut = true;
    return s;
}
// Process output is not ours: make it valid UTF-8 (invalid bytes become '?').
std::string ValidText(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t len = SJ::detail::Utf8Length(s, i);
        if (!len) {
            out += '?';
            ++i;
            continue;
        }
        out.append(s.data() + i, len);
        i += len;
    }
    return out;
}
std::string Trim(std::string s) {
    const auto ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && ws(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}
std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return out;
}
bool WordChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
// `needle` (lowercase) inside `hay` (lowercase); wordStart / wordEnd ask for a word boundary on that side.
bool Has(std::string_view hay, std::string_view needle, bool wordStart = false, bool wordEnd = false) {
    for (std::size_t at = hay.find(needle); at != std::string_view::npos; at = hay.find(needle, at + 1)) {
        const std::size_t end = at + needle.size();
        if (wordStart && at > 0 && WordChar(hay[at - 1])) continue;
        if (wordEnd && end < hay.size() && WordChar(hay[end])) continue;
        return true;
    }
    return false;
}
bool HasAny(std::string_view hay, std::initializer_list<std::string_view> needles, bool wordStart = false, bool wordEnd = false) {
    for (const auto n : needles)
        if (Has(hay, n, wordStart, wordEnd)) return true;
    return false;
}
// "<lead>[: ]+5xx" with a word boundary after the three digits.
bool HasServerStatus(std::string_view hay, std::string_view lead) {
    for (std::size_t at = hay.find(lead); at != std::string_view::npos; at = hay.find(lead, at + 1)) {
        std::size_t i = at + lead.size();
        const std::size_t sep = i;
        while (i < hay.size() && (hay[i] == ':' || hay[i] == ' ' || hay[i] == '\t')) ++i;
        if (i == sep || i + 3 > hay.size() || hay[i] != '5') continue;
        if (hay[i + 1] < '0' || hay[i + 1] > '9' || hay[i + 2] < '0' || hay[i + 2] > '9') continue;
        if (i + 3 < hay.size() && WordChar(hay[i + 3])) continue;
        return true;
    }
    return false;
}
// "run <word> login" or "run <word> auth login".
bool HasRunLogin(std::string_view hay) {
    for (std::size_t at = hay.find("run "); at != std::string_view::npos; at = hay.find("run ", at + 1)) {
        if (at > 0 && WordChar(hay[at - 1])) continue;
        std::size_t i = at + 4;
        const std::size_t word = i;
        while (i < hay.size() && (WordChar(hay[i]) || hay[i] == '-')) ++i;
        if (i == word) continue;
        std::string_view rest = hay.substr(i);
        if (rest.rfind(" auth", 0) == 0) rest.remove_prefix(5);
        if (rest.rfind(" login", 0) == 0 && (rest.size() == 6 || !WordChar(rest[6]))) return true;
    }
    return false;
}

// ---------------------------------------------------------------- JSON access
const SJ::Value* Member(const SJ::Value* v, std::string_view key) { return v ? v->Get(key) : nullptr; }
std::string Str(const SJ::Value* v, std::string_view key, std::size_t max = MaxTextBytes) {
    const SJ::Value* m = Member(v, key);
    return m && m->IsString() ? Clip(m->string, max) : std::string();
}
bool Flag(const SJ::Value* v, std::string_view key) {
    const SJ::Value* m = Member(v, key);
    return m && m->IsBool() && m->boolean;
}
const SJ::Value* Object(const SJ::Value* v, std::string_view key) {
    const SJ::Value* m = Member(v, key);
    return m && m->IsObject() ? m : nullptr;
}
const SJ::Value* Array(const SJ::Value* v, std::string_view key) {
    const SJ::Value* m = Member(v, key);
    return m && m->IsArray() ? m : nullptr;
}
SJ::Limits LineLimits() {
    SJ::Limits l;
    l.maxBytes = MaxLineBytes;
    l.maxDepth = 64;
    l.maxStringBytes = MaxLineBytes;
    l.maxItems = 1u << 20;
    return l;
}

// ---------------------------------------------------------------- parsers
Capabilities ParseCapabilities(const SJ::Value* caps) {
    Capabilities c;
    const SJ::Value* prompt = Object(caps, "promptCapabilities");
    c.image = Flag(prompt, "image");
    c.audio = Flag(prompt, "audio");
    c.embeddedContext = Flag(prompt, "embeddedContext");
    c.loadSession = Flag(caps, "loadSession");
    c.resume = Object(Object(caps, "sessionCapabilities"), "resume") != nullptr;
    const SJ::Value* mcp = Object(caps, "mcpCapabilities");
    c.httpMcp = Flag(mcp, "http");
    c.sseMcp = Flag(mcp, "sse");
    return c;
}
std::vector<AuthMethod> ParseAuthMethods(const SJ::Value* list) {
    std::vector<AuthMethod> out;
    if (!list || !list->IsArray()) return out;
    for (const SJ::Value& m : list->items) {
        if (!m.IsObject() || out.size() >= 32) continue;
        AuthMethod a;
        a.id = Str(&m, "id", 256);
        a.name = Str(&m, "name", 256);
        a.description = Str(&m, "description", 1024);
        if (a.id.empty()) continue;
        a.terminal = Str(&m, "type", 32) == "terminal";
        if (const SJ::Value* args = Array(&m, "args"))
            for (const SJ::Value& arg : args->items)
                if (arg.IsString() && a.terminalArgs.size() < 32) a.terminalArgs.push_back(Clip(arg.string, 1024));
        if (const SJ::Value* env = Object(&m, "env"))
            for (const auto& e : env->members)
                if (e.second.IsString() && a.terminalEnv.size() < 32) a.terminalEnv.emplace_back(Clip(e.first, 256), Clip(e.second.string, 4096));
        out.push_back(std::move(a));
    }
    return out;
}
ModeState ParseModes(const SJ::Value* modes) {
    ModeState s;
    if (!modes || !modes->IsObject()) return s;
    s.currentId = Str(modes, "currentModeId", 256);
    if (const SJ::Value* list = Array(modes, "availableModes"))
        for (const SJ::Value& m : list->items) {
            if (!m.IsObject() || s.available.size() >= 64) continue;
            Mode mode{Str(&m, "id", 256), Str(&m, "name", 256), Str(&m, "description", 1024)};
            if (!mode.id.empty()) s.available.push_back(std::move(mode));
        }
    return s;
}
bool ParseChoice(const SJ::Value& c, const std::string& group, std::vector<ConfigChoice>& out) {
    if (!c.IsObject()) return false;
    const SJ::Value* value = c.Get("value");
    const SJ::Value* name = c.Get("name");
    if (!value || !value->IsString() || !name || !name->IsString()) return false;
    if (out.size() < 256) out.push_back({Clip(value->string, 256), Clip(name->string, 256), Str(&c, "description", 1024), group});
    return true;
}
std::vector<ConfigOption> ParseConfigOptions(const SJ::Value* list) {
    std::vector<ConfigOption> out;
    if (!list || !list->IsArray()) return out;
    for (const SJ::Value& o : list->items) {
        if (!o.IsObject() || out.size() >= 64) continue;
        ConfigOption opt;
        opt.id = Str(&o, "id", 256);
        opt.name = Str(&o, "name", 256);
        opt.description = Str(&o, "description", 1024);
        opt.category = Str(&o, "category", 64);
        opt.type = Str(&o, "type", 32);
        if (opt.id.empty()) continue;
        const SJ::Value* current = o.Get("currentValue");
        if (opt.type == "boolean") {
            if (!current || !current->IsBool()) continue;
            opt.currentValue = current->boolean ? "true" : "false";
        } else if (opt.type == "select") {
            if (!current || !current->IsString()) continue;
            opt.currentValue = Clip(current->string, 256);
            // Grouped lists (e.g. by provider) are flattened; each choice keeps its group's name.
            if (const SJ::Value* choices = Array(&o, "options"))
                for (const SJ::Value& entry : choices->items) {
                    if (ParseChoice(entry, "", opt.choices)) continue;
                    if (const SJ::Value* inner = Array(&entry, "options")) {
                        const std::string group = Str(&entry, "name", 256);
                        for (const SJ::Value& c : inner->items) ParseChoice(c, group, opt.choices);
                    }
                }
        } else {
            continue;  // a kind of option this client does not know
        }
        out.push_back(std::move(opt));
    }
    return out;
}
std::vector<Command> ParseCommands(const SJ::Value* list) {
    std::vector<Command> out;
    if (!list || !list->IsArray()) return out;
    for (const SJ::Value& c : list->items) {
        if (!c.IsObject() || out.size() >= 256) continue;
        Command cmd{Str(&c, "name", 256), Str(&c, "description", 1024), Str(Object(&c, "input"), "hint", 256)};
        if (!cmd.name.empty()) out.push_back(std::move(cmd));
    }
    return out;
}
ToolCall ParseToolCall(const SJ::Value* t) {
    ToolCall call;
    call.id = Str(t, "toolCallId", 512);
    if (const SJ::Value* v = Member(t, "title"); v && v->IsString()) { call.hasTitle = true; call.title = Clip(v->string, 16u << 10); }
    if (const SJ::Value* v = Member(t, "kind"); v && v->IsString()) { call.hasKind = true; call.kind = Clip(v->string, 64); }
    if (const SJ::Value* v = Member(t, "status"); v && v->IsString()) { call.hasStatus = true; call.status = Clip(v->string, 64); }
    if (const SJ::Value* content = Array(t, "content")) {
        call.hasContent = true;
        for (const SJ::Value& item : content->items) {
            if (!item.IsObject()) continue;
            const std::string type = Str(&item, "type", 32);
            if (type == "content") {
                const SJ::Value* block = Object(&item, "content");
                if (Str(block, "type", 32) != "text") continue;
                const SJ::Value* text = Member(block, "text");
                if (!text || !text->IsString() || call.text.size() >= MaxTextBytes) continue;
                if (!call.text.empty()) call.text += "\n\n";
                call.text += text->string;
                call.text = Clip(std::move(call.text), MaxTextBytes);
            } else if (type == "diff" && call.diffs.size() < 64) {
                const SJ::Value* path = item.Get("path");
                const SJ::Value* newText = item.Get("newText");
                if (!path || !path->IsString() || !newText || !newText->IsString()) continue;
                Diff d;
                d.path = Clip(path->string, 4096);
                d.newText = Clip(newText->string, MaxDiffBytes, &d.truncated);
                if (const SJ::Value* old = item.Get("oldText"); old && old->IsString()) {
                    d.hasOldText = true;
                    d.oldText = Clip(old->string, MaxDiffBytes, &d.truncated);
                }
                call.diffs.push_back(std::move(d));
            }
        }
    }
    if (const SJ::Value* locations = Array(t, "locations")) {
        call.hasLocations = true;
        for (const SJ::Value& l : locations->items) {
            const SJ::Value* path = Member(&l, "path");
            if (!path || !path->IsString() || call.locations.size() >= 256) continue;
            ToolLocation loc;
            loc.path = Clip(path->string, 4096);
            if (const SJ::Value* line = l.Get("line"); line && line->IsNumber() && line->integral && line->integer >= 0) loc.line = line->integer;
            call.locations.push_back(std::move(loc));
        }
    }
    if (const SJ::Value* raw = Member(t, "rawInput"); raw && !raw->IsNull()) {
        std::string json = SJ::Serialize(*raw);
        if (json.size() <= (64u << 10)) call.rawInput = std::move(json);
    }
    return call;
}

// ---------------------------------------------------------------- questions (elicitation form)
struct FormChoice {
    std::string value, name, description;
};
struct FormQuestion {
    std::string id, title, description;
    std::vector<FormChoice> choices;
};
// A field's choices when it asks for a single pick from a list; empty otherwise.
std::vector<FormChoice> FieldChoices(const SJ::Value& field) {
    std::vector<FormChoice> out;
    if (!field.IsObject() || Str(&field, "type", 32) != "string") return out;
    if (const SJ::Value* oneOf = Array(&field, "oneOf"))
        for (const SJ::Value& o : oneOf->items) {
            const SJ::Value* value = Member(&o, "const");
            if (!value || !value->IsString() || out.size() >= 64) continue;
            const std::string title = Str(&o, "title", 256);
            out.push_back({Clip(value->string, 1024), title.empty() ? Clip(value->string, 256) : title, Str(&o, "description", 1024)});
        }
    if (!out.empty()) return out;
    if (const SJ::Value* values = Array(&field, "enum"))
        for (const SJ::Value& v : values->items)
            if (v.IsString() && out.size() < 64) out.push_back({Clip(v.string, 1024), Clip(v.string, 256), ""});
    return out;
}
// The questions of a form, or false when the form is not one we can ask: every question must be one pick from a
// list. An optional field without choices is left out; a required one makes the form unaskable.
bool FormQuestions(const SJ::Value* schema, std::vector<FormQuestion>& out) {
    out.clear();
    const SJ::Value* properties = Object(schema, "properties");
    if (!properties) return false;
    std::vector<std::string> required;
    if (const SJ::Value* req = Array(schema, "required"))
        for (const SJ::Value& r : req->items)
            if (r.IsString()) required.push_back(r.string);
    for (const auto& field : properties->members) {
        std::vector<FormChoice> choices = FieldChoices(field.second);
        if (choices.empty()) {
            if (std::find(required.begin(), required.end(), field.first) != required.end()) return false;
            continue;
        }
        if (out.size() >= 16) return false;
        out.push_back({Clip(field.first, 256), Trim(Str(&field.second, "title", 1024)), Trim(Str(&field.second, "description", 4096)), std::move(choices)});
    }
    return !out.empty();
}

// ---------------------------------------------------------------- process helpers
std::string LastErrorText(const char* api, DWORD code) { return std::string(api) + " " + std::to_string(code); }

struct EnvEntry {
    std::wstring name, value;
};
void AddEnv(std::vector<EnvEntry>& entries, const std::wstring& name, const std::wstring& value) {
    for (auto& e : entries)
        if (CompareStringOrdinal(e.name.c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL) {
            e.value = value;
            return;
        }
    entries.push_back({name, value});
}
void ReadBlock(const wchar_t* block, std::vector<EnvEntry>& entries) {
    for (const wchar_t* p = block; p && *p; p += wcslen(p) + 1) {
        const std::wstring var = p;
        const std::size_t eq = var.find(L'=', 1);  // "=C:=C:\dir" entries keep their leading '='
        if (eq == std::wstring::npos) continue;
        AddEnv(entries, var.substr(0, eq), var.substr(eq + 1));
    }
}

}  // namespace

// ================================================================ pure helpers
const char* ErrorCodeName(ErrorCode code) {
    switch (code) {
    case ErrorCode::None: return "none";
    case ErrorCode::NotInstalled: return "not_installed";
    case ErrorCode::CliTooOld: return "cli_too_old";
    case ErrorCode::NeedsPrepare: return "needs_prepare";
    case ErrorCode::SpawnFailed: return "spawn_failed";
    case ErrorCode::ProcessExited: return "process_exited";
    case ErrorCode::Timeout: return "timeout";
    case ErrorCode::Protocol: return "protocol";
    case ErrorCode::AuthRequired: return "auth_required";
    case ErrorCode::AgentError: return "agent_error";
    case ErrorCode::NotReady: return "not_ready";
    case ErrorCode::ContextTooLong: return "context_too_long";
    case ErrorCode::UsageLimit: return "usage_limit";
    case ErrorCode::Overloaded: return "overloaded";
    case ErrorCode::RateLimited: return "rate_limited";
    case ErrorCode::ServerError: return "server_error";
    case ErrorCode::Network: return "network";
    }
    return "none";
}

// Phrases agents use for a missing or expired login. Whole words only: a bare "auth" would also match
// "author" or "OAuth".
bool LooksLikeAuthError(std::string_view text) {
    const std::string t = Lower(text);
    // i18n-exempt-begin: wording matched in the agents' own (English) error text
    if (HasAny(t, {"auth required", "auth is required", "authentication required", "authentication is required"}, true)) return true;
    if (HasAny(t, {"authenticate", "authentication"}, true, true)) return true;
    if (HasAny(t, {"unauthorized", "unauthorised", "not logged in", "not signed in", "no credentials", "run setup first"}, true)) return true;
    if (HasAny(t, {"login required", "log in required", "login is required", "log in is required", "signin required",
                   "sign in required", "signin is required", "sign in is required"}, true)) return true;
    if (HasAny(t, {"please login", "please log in", "please signin", "please sign in"}, true)) return true;
    for (const std::string_view state : {"invalid ", "missing ", "expired "})
        for (const std::string_view what : {"api key", "credential", "token"})
            if (Has(t, std::string(state) + std::string(what), true)) return true;
    // i18n-exempt-end
    return HasRunLogin(t) || Has(t, "401", true, true);
}

// Known ways a turn fails, checked in order: a quota error can come as a 429, so the account's limit is checked
// before the rate limit. Matched on the agent's own text, which often carries the provider's raw JSON.
ErrorCode PlainFailure(std::string_view text) {
    const std::string t = Lower(text);
    // i18n-exempt-begin: wording matched in the agents' own (English) error text
    if (HasAny(t, {"context length", "context window", "context_length", "context_window", "contextlength", "contextwindow",
                   "context-length", "context-window", "prompt is too long", "input is too long", "too many tokens",
                   "too many input tokens", "maximum context"})) return ErrorCode::ContextTooLong;
    if (HasAny(t, {"credit balance", "billing", "insufficient quota", "insufficient_quota", "insufficient funds", "insufficient_funds",
                   "insufficient credit", "insufficient_credit", "quota exceeded", "payment required", "usage limit"}) ||
        Has(t, "402", true, true)) return ErrorCode::UsageLimit;
    if (Has(t, "overloaded") || Has(t, "529", true, true)) return ErrorCode::Overloaded;
    if (HasAny(t, {"rate limit", "rate_limit", "rate-limit", "ratelimit", "too many requests"}) || Has(t, "429", true, true)) return ErrorCode::RateLimited;
    if (HasServerStatus(t, "api error") || HasServerStatus(t, "status code") || HasServerStatus(t, "status") || HasServerStatus(t, "http") ||
        HasAny(t, {"internal server error", "bad gateway", "service unavailable", "gateway timeout", "gateway time-out", "\"api_error\""}))
        return ErrorCode::ServerError;
    if (HasAny(t, {"econnreset", "econnrefused", "enotfound", "eai_again", "etimedout", "enetunreach"}, true, true) ||
        HasAny(t, {"socket hang up", "network error", "fetch failed", "connection error", "connection refused", "connection reset"}) ||
        HasAny(t, {"timed out", "timedout", "time out", "timeout"}, false, true)) return ErrorCode::Network;
    // i18n-exempt-end
    return ErrorCode::None;
}

// « Ton agent »: the adapter is alive but the agent's CLI behind it closed before answering — the Claude Agent SDK says
// « Query closed before response received » (the CLI quit: an expired sign-in, a crash, an antivirus, an update replacing
// it). Not an unknown agent error: the host checks the agent again and says the real cause (chat-host-turn.h).
bool LooksLikeCliClosed(std::string_view text) {
    const std::string t = Lower(text);
    // i18n-exempt-begin: wording matched in the agents' own (English) error text
    return HasAny(t, {"query closed before response", "closed before response received", "process exited unexpectedly",
                      "claude code process exited", "cli process exited", "process exited with code"});
    // i18n-exempt-end
}

Error ClassifyAgentError(int rpcCode, const std::string& message, const std::string& dataDetail) {
    Error e;
    e.rpcCode = rpcCode;
    // Many agents put the useful part in `data` behind a generic "Internal error".
    e.detail = Clip(ValidText(Trim(dataDetail).empty() ? Trim(message) : Trim(dataDetail)), MaxDetailBytes);
    if (rpcCode == -32000 || LooksLikeAuthError(e.detail) || LooksLikeAuthError(message)) e.code = ErrorCode::AuthRequired;
    else if (LooksLikeCliClosed(e.detail) || LooksLikeCliClosed(message)) e.code = ErrorCode::ProcessExited;
    else if (const ErrorCode plain = PlainFailure(e.detail); plain != ErrorCode::None) e.code = plain;
    else e.code = ErrorCode::AgentError;
    return e;
}

const ConfigOption* FindModelOption(const std::vector<ConfigOption>& options) {
    for (const auto& o : options)
        if (o.type == "select" && o.category == "model" && o.id == "model") return &o;
    for (const auto& o : options)
        if (o.type == "select" && o.category == "model") return &o;
    return nullptr;
}
const ConfigOption* FindEffortOption(const std::vector<ConfigOption>& options) {
    for (const auto& o : options)
        if (o.type == "select" && o.category == "thought_level") return &o;
    return nullptr;
}
const ConfigOption* FindModeOption(const std::vector<ConfigOption>& options) {
    for (const auto& o : options)
        if (o.type == "select" && o.category == "mode") return &o;
    return nullptr;
}

std::wstring QuoteArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
        } else {
            out.append(backslashes, L'\\');
            out += arg[i];
        }
    }
    return out + L"\"";
}

std::wstring BuildEnvironmentBlock(const LaunchSpec& spec) {
    std::vector<EnvEntry> entries;
    bool fresh = false;
    if (spec.freshUserEnvironment) {
        // The user's environment as Windows would build it now: a PATH changed since the browser started is seen.
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, &token)) {
            void* block = nullptr;
            if (CreateEnvironmentBlock(&block, token, FALSE)) {
                ReadBlock(static_cast<const wchar_t*>(block), entries);
                DestroyEnvironmentBlock(block);
                fresh = !entries.empty();
            }
            CloseHandle(token);
        }
    }
    if (!fresh) {
        if (wchar_t* block = GetEnvironmentStringsW()) {
            ReadBlock(block, entries);
            FreeEnvironmentStringsW(block);
        }
    }
    for (const auto& name : spec.unsetEnv)
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const EnvEntry& e) { return CompareStringOrdinal(e.name.c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL; }),
                      entries.end());
    for (const auto& kv : spec.env)
        if (!kv.first.empty() && kv.first.find(L'=') == std::wstring::npos && kv.first.find(L'\0') == std::wstring::npos &&
            kv.second.find(L'\0') == std::wstring::npos)
            AddEnv(entries, kv.first, kv.second);
    std::stable_sort(entries.begin(), entries.end(), [](const EnvEntry& a, const EnvEntry& b) {
        return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    std::wstring block;
    for (const auto& e : entries) {
        block += e.name;
        block += L'=';
        block += e.value;
        block.push_back(L'\0');
    }
    if (entries.empty()) block.push_back(L'\0');
    block.push_back(L'\0');
    return block;
}

// ================================================================ session
namespace {

enum class Req { Initialize, Authenticate, Resume, Load, New, Prompt, SetMode, SetConfig };
struct Pending {
    Req kind;
    bool timed = false;
    Clock::time_point deadline;
    std::string a, b;  // SetMode: mode id; SetConfig: option id, value
};
// An inbound request waiting for the user.
struct Ask {
    SJ::Value rpcId;
    bool question = false;
    std::vector<FormQuestion> questions;
    std::size_t index = 0;
    std::vector<std::pair<std::string, std::string>> answers;
    std::string message;
    std::string current;  // request id of the question on screen
};

}  // namespace

struct Run;

struct SessionImpl {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<Event> queue;
    std::function<void()> wake;
    unsigned generation = 0;
    std::shared_ptr<Run> run;  // the live run, or the last one (WaitStopped)
    bool hasOptions = false;
    StartOptions options;
    std::vector<AuthMethod> authMethods;  // from the last Started event

    void Push(unsigned gen, Event&& e) {
        std::function<void()> call;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (gen != generation) return;
            if (e.type == Event::Type::Started) authMethods = e.started.authMethods;
            if (queue.empty()) call = wake;
            queue.push_back(std::move(e));
        }
        cv.notify_all();
        if (call) call();
    }
};

struct Run : std::enable_shared_from_this<Run> {
    std::weak_ptr<SessionImpl> owner;
    unsigned generation = 0;
    StartOptions opts;
    bool signIn = false;

    HANDLE process = nullptr, job = nullptr, stdinW = nullptr, stdoutR = nullptr, stderrR = nullptr;
    HANDLE stoppedEvent = nullptr;  // set when Main() has ended and every handle is closed
    std::atomic<unsigned long> pid{0};
    std::thread pump, errThread;

    std::mutex mu;  // everything below
    std::condition_variable cv;
    std::deque<std::string> outgoing;
    bool closeInput = false, done = false, stopRequested = false, quiet = false;
    std::thread killer;
    std::map<std::int64_t, Pending> pending;
    std::int64_t nextId = 1;
    Phase phase = Phase::Starting;
    bool startAnswered = false, live = false, cancelRequested = false;
    std::string sessionId;
    StartResult info;
    std::size_t resumeStep = 0;
    std::map<std::string, Ask> asks;
    unsigned askSeq = 0, warnings = 0;

    std::mutex errMu;
    std::string stderrTail;

    Run() { stoppedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~Run() {
        if (stoppedEvent) CloseHandle(stoppedEvent);
    }

    // ------------------------------------------------------------ events
    void Emit(Event&& e) {
        {
            std::lock_guard<std::mutex> lock(mu);
            if (quiet) return;
        }
        if (auto impl = owner.lock()) impl->Push(generation, std::move(e));
    }
    void Warn(const std::string& text) {
        {
            std::lock_guard<std::mutex> lock(mu);
            if (++warnings > 20) return;  // a broken agent must not flood the queue
        }
        Event e;
        e.type = Event::Type::Warning;
        e.text = Clip(ValidText(text), 512);
        Emit(std::move(e));
    }
    std::string StderrTail() {
        std::lock_guard<std::mutex> lock(errMu);
        return Trim(ValidText(stderrTail));
    }

    // ------------------------------------------------------------ stopping
    // Closes the agent's stdin, then kills the whole job after the grace period. Any thread, idempotent.
    void Stop(bool silence) {
        {
            std::lock_guard<std::mutex> lock(mu);
            if (silence) quiet = true;
            phase = Phase::Stopped;
            if (stopRequested) return;
            stopRequested = true;
            closeInput = true;
            outgoing.clear();
            if (process) {
                auto self = shared_from_this();
                const unsigned grace = opts.timeouts.exitGraceMs;
                killer = std::thread([self, grace] {
                    WaitForSingleObject(self->process, grace);
                    TerminateJobObject(self->job, 1);
                });
            }
        }
        cv.notify_all();
    }
    // Answers the Start once, then stops the process.
    void FailStart(StartState state, Error error) {
        Event e;
        e.type = Event::Type::Started;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (startAnswered) return;
            startAnswered = true;
            info.state = state;
            info.error = std::move(error);
            e.started = info;
        }
        Emit(std::move(e));
        Stop(true);
    }

    // ------------------------------------------------------------ sending
    void SendLocked(const SJ::Value& message) {
        if (stopRequested) return;
        outgoing.push_back(SJ::Serialize(message) + "\n");
    }
    void Send(const SJ::Value& message) {
        {
            std::lock_guard<std::mutex> lock(mu);
            SendLocked(message);
        }
        cv.notify_all();
    }
    void RequestLocked(Req kind, const char* method, SJ::Value params, unsigned timeoutMs, std::string a = {}, std::string b = {}) {
        const std::int64_t id = nextId++;
        Pending p;
        p.kind = kind;
        p.timed = timeoutMs != 0;
        p.deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        p.a = std::move(a);
        p.b = std::move(b);
        pending.emplace(id, std::move(p));
        SJ::Value m = SJ::Value::Obj();
        m.Set("jsonrpc", SJ::Value::Str("2.0"));
        m.Set("id", SJ::Value::Int(id));
        m.Set("method", SJ::Value::Str(method));
        m.Set("params", std::move(params));
        SendLocked(m);
    }
    void Request(Req kind, const char* method, SJ::Value params, unsigned timeoutMs, std::string a = {}, std::string b = {}) {
        {
            std::lock_guard<std::mutex> lock(mu);
            RequestLocked(kind, method, std::move(params), timeoutMs, std::move(a), std::move(b));
        }
        cv.notify_all();
    }
    static SJ::Value Reply(const SJ::Value& id, SJ::Value result) {
        SJ::Value m = SJ::Value::Obj();
        m.Set("jsonrpc", SJ::Value::Str("2.0"));
        m.Set("id", id);
        m.Set("result", std::move(result));
        return m;
    }
    static SJ::Value ReplyError(const SJ::Value& id, int code, const char* message) {
        SJ::Value err = SJ::Value::Obj();
        err.Set("code", SJ::Value::Int(code));
        err.Set("message", SJ::Value::Str(message));
        SJ::Value m = SJ::Value::Obj();
        m.Set("jsonrpc", SJ::Value::Str("2.0"));
        m.Set("id", id);
        m.Set("error", std::move(err));
        return m;
    }
    static SJ::Value PermissionOutcome(const std::string& optionId) {
        SJ::Value outcome = SJ::Value::Obj();
        if (optionId.empty()) {
            outcome.Set("outcome", SJ::Value::Str("cancelled"));
        } else {
            outcome.Set("outcome", SJ::Value::Str("selected"));
            outcome.Set("optionId", SJ::Value::Str(optionId));
        }
        SJ::Value result = SJ::Value::Obj();
        result.Set("outcome", std::move(outcome));
        return result;
    }
    static SJ::Value Action(const char* action) {
        SJ::Value result = SJ::Value::Obj();
        result.Set("action", SJ::Value::Str(action));
        return result;
    }

    // ------------------------------------------------------------ start sequence
    SJ::Value McpServersJson() const {
        SJ::Value list = SJ::Value::Arr();
        for (const McpServer& s : opts.mcpServers) {
            SJ::Value server = SJ::Value::Obj();
            server.Set("name", SJ::Value::Str(s.name));
            server.Set("command", SJ::Value::Str(s.command));
            SJ::Value args = SJ::Value::Arr();
            for (const auto& a : s.args) args.Push(SJ::Value::Str(a));
            server.Set("args", std::move(args));
            SJ::Value env = SJ::Value::Arr();
            for (const auto& kv : s.env) {
                SJ::Value var = SJ::Value::Obj();
                var.Set("name", SJ::Value::Str(kv.first));
                var.Set("value", SJ::Value::Str(kv.second));
                env.Push(std::move(var));
            }
            server.Set("env", std::move(env));
            list.Push(std::move(server));
        }
        return list;
    }
    void SendInitialize() {
        SJ::Value fs = SJ::Value::Obj();
        fs.Set("readTextFile", SJ::Value::Bool(false));
        fs.Set("writeTextFile", SJ::Value::Bool(false));
        SJ::Value auth = SJ::Value::Obj();
        auth.Set("terminal", SJ::Value::Bool(true));
        SJ::Value caps = SJ::Value::Obj();
        caps.Set("fs", std::move(fs));
        caps.Set("terminal", SJ::Value::Bool(false));
        caps.Set("auth", std::move(auth));
        if (opts.questions) {
            SJ::Value elicitation = SJ::Value::Obj();
            elicitation.Set("form", SJ::Value::Obj());
            caps.Set("elicitation", std::move(elicitation));
        }
        SJ::Value client = SJ::Value::Obj();
        client.Set("name", SJ::Value::Str(opts.clientName));
        client.Set("version", SJ::Value::Str(opts.clientVersion));
        SJ::Value params = SJ::Value::Obj();
        params.Set("protocolVersion", SJ::Value::Int(ProtocolVersion));
        params.Set("clientCapabilities", std::move(caps));
        params.Set("clientInfo", std::move(client));
        Request(Req::Initialize, "initialize", std::move(params), opts.timeouts.initializeMs);
    }
    // session/resume, then session/load, then a new session.
    void OpenSession() {
        Capabilities caps;
        std::size_t step;
        {
            std::lock_guard<std::mutex> lock(mu);
            caps = info.capabilities;
            step = resumeStep++;
        }
        SJ::Value params = SJ::Value::Obj();
        params.Set("cwd", SJ::Value::Str(opts.cwd));
        params.Set("mcpServers", McpServersJson());
        if (!opts.resumeSessionId.empty()) {
            std::vector<Req> how;
            if (caps.resume) how.push_back(Req::Resume);
            if (caps.loadSession) how.push_back(Req::Load);
            if (step < how.size()) {
                params.Set("sessionId", SJ::Value::Str(opts.resumeSessionId));
                Request(how[step], how[step] == Req::Resume ? "session/resume" : "session/load", std::move(params), opts.timeouts.sessionMs);
                return;
            }
        }
        Request(Req::New, "session/new", std::move(params), opts.timeouts.sessionMs);
    }
    void SessionOpened(const SJ::Value* result, const std::string& id, bool resumed) {
        Event e;
        e.type = Event::Type::Started;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (startAnswered || stopRequested) return;
            startAnswered = true;
            sessionId = id;
            live = true;
            phase = Phase::Ready;
            info.state = StartState::Ready;
            info.error = Error();
            info.sessionId = id;
            info.resumed = resumed;
            info.modes = ParseModes(Member(result, "modes"));
            info.configOptions = ParseConfigOptions(Member(result, "configOptions"));
            SyncModeFromOptionLocked();
            e.started = info;
        }
        Emit(std::move(e));
    }
    // Agents that offer the mode both ways should keep the two in step, but may report a change through one only.
    bool SyncModeFromOptionLocked() {
        const ConfigOption* mode = FindModeOption(info.configOptions);
        if (!mode || info.modes.available.empty() || info.modes.currentId == mode->currentValue) return false;
        info.modes.currentId = mode->currentValue;
        return true;
    }
    bool SyncOptionFromModeLocked() {
        bool changed = false;
        for (auto& o : info.configOptions)
            if (o.type == "select" && o.category == "mode" && o.currentValue != info.modes.currentId) {
                o.currentValue = info.modes.currentId;
                changed = true;
            }
        return changed;
    }
    void EmitSettings(bool modes, bool options) {
        Event m, o;
        {
            std::lock_guard<std::mutex> lock(mu);
            m.type = Event::Type::ModesChanged;
            m.modes = info.modes;
            o.type = Event::Type::ConfigOptionsChanged;
            o.configOptions = info.configOptions;
        }
        if (modes) Emit(std::move(m));
        if (options) Emit(std::move(o));
    }

    // ------------------------------------------------------------ responses
    static Error ErrorOf(const SJ::Value& error) {
        const SJ::Value* code = error.Get("code");
        const int rpc = code && code->IsNumber() && code->integral ? int(code->integer) : 0;
        std::string detail;
        if (const SJ::Value* data = error.Get("data")) {
            if (data->IsString()) detail = data->string;
            else if (data->IsObject())
                for (const char* key : {"details", "error", "message"}) {
                    detail = Trim(Str(data, key, MaxDetailBytes));
                    if (!detail.empty()) break;
                }
        }
        return ClassifyAgentError(rpc, Str(&error, "message", MaxDetailBytes), detail);
    }
    void OnResponse(std::int64_t id, const SJ::Value* result, const SJ::Value* error) {
        Pending p;
        {
            std::lock_guard<std::mutex> lock(mu);
            const auto it = pending.find(id);
            if (it == pending.end()) return;
            p = std::move(it->second);
            pending.erase(it);
        }
        const bool failed = error && error->IsObject();
        const Error err = failed ? ErrorOf(*error) : Error();
        switch (p.kind) {
        case Req::Initialize: {
            if (failed) {
                // Some agents refuse to start at all without a login.
                FailStart(err.code == ErrorCode::AuthRequired ? StartState::AuthRequired : StartState::Failed, err);
                return;
            }
            const SJ::Value* version = Member(result, "protocolVersion");
            const std::int64_t v = version && version->IsNumber() && version->integral ? version->integer : -1;
            {
                std::lock_guard<std::mutex> lock(mu);
                info.protocolVersion = int(v);
                info.capabilities = ParseCapabilities(Object(result, "agentCapabilities"));
                info.authMethods = ParseAuthMethods(Member(result, "authMethods"));
                const SJ::Value* agent = Object(result, "agentInfo");
                info.agentName = Str(agent, "name", 256);
                info.agentTitle = Str(agent, "title", 256);
                info.agentVersion = Str(agent, "version", 64);
            }
            if (v != ProtocolVersion) {
                Error e;
                e.code = ErrorCode::Protocol;
                e.detail = "protocolVersion " + std::to_string(v);
                FailStart(StartState::Failed, e);
                return;
            }
            if (!opts.authenticateMethodId.empty()) {
                SJ::Value params = SJ::Value::Obj();
                params.Set("methodId", SJ::Value::Str(opts.authenticateMethodId));
                Request(Req::Authenticate, "authenticate", std::move(params), opts.timeouts.authenticateMs);
            } else {
                OpenSession();
            }
            return;
        }
        case Req::Authenticate:
            if (failed) FailStart(StartState::AuthRequired, err);  // still signed out; error says why
            else OpenSession();
            return;
        case Req::Resume:
        case Req::Load:
            if (!failed) SessionOpened(result, opts.resumeSessionId, true);
            else if (err.code == ErrorCode::AuthRequired) FailStart(StartState::AuthRequired, err);
            else OpenSession();  // the session is gone or cannot be read: next way, then a new one
            return;
        case Req::New: {
            if (failed) {
                FailStart(err.code == ErrorCode::AuthRequired ? StartState::AuthRequired : StartState::Failed, err);
                return;
            }
            const std::string id = Str(result, "sessionId", 512);
            if (id.empty()) {
                Error e;
                e.code = ErrorCode::Protocol;
                e.detail = "session/new: sessionId";
                FailStart(StartState::Failed, e);
                return;
            }
            SessionOpened(result, id, false);
            return;
        }
        case Req::Prompt: {
            Event e;
            e.type = Event::Type::TurnEnded;
            bool cancelled;
            {
                std::lock_guard<std::mutex> lock(mu);
                if (phase == Phase::Prompting) phase = Phase::Ready;
                cancelled = cancelRequested;
                cancelRequested = false;
            }
            DismissAsks();
            if (!failed) {
                e.stopReason = Str(result, "stopReason", 64);
                if (e.stopReason.empty()) {
                    e.error.code = ErrorCode::Protocol;
                    e.error.detail = "session/prompt: stopReason";
                }
            } else if (cancelled && (err.rpcCode == -32800 || err.code == ErrorCode::AgentError)) {
                e.stopReason = "cancelled";  // an agent that answers a cancelled turn with an error
            } else {
                e.error = err;
            }
            Emit(std::move(e));
            return;
        }
        case Req::SetMode: {
            if (failed) {
                RequestFailed("set_mode", err);
                return;
            }
            bool options;
            {
                std::lock_guard<std::mutex> lock(mu);
                info.modes.currentId = p.a;
                options = SyncOptionFromModeLocked();
            }
            EmitSettings(true, options);
            return;
        }
        case Req::SetConfig: {
            if (failed) {
                RequestFailed("set_config_option", err);
                return;
            }
            bool modes;
            {
                std::lock_guard<std::mutex> lock(mu);
                if (const SJ::Value* list = Array(result, "configOptions")) {
                    info.configOptions = ParseConfigOptions(list);
                } else {
                    for (auto& o : info.configOptions)
                        if (o.id == p.a) o.currentValue = p.b;
                }
                modes = SyncModeFromOptionLocked();
            }
            EmitSettings(modes, true);
            return;
        }
        }
    }
    void RequestFailed(const char* request, const Error& error) {
        Event e;
        e.type = Event::Type::RequestFailed;
        e.request = request;
        e.error = error;
        Emit(std::move(e));
    }
    void OnTimeout(const Pending& p) {
        Error e;
        e.code = ErrorCode::Timeout;
        switch (p.kind) {
        case Req::Initialize: e.detail = "initialize"; FailStart(StartState::Failed, e); break;
        case Req::Authenticate: e.detail = "authenticate"; FailStart(StartState::AuthRequired, e); break;
        case Req::Resume: e.detail = "session/resume"; FailStart(StartState::Failed, e); break;
        case Req::Load: e.detail = "session/load"; FailStart(StartState::Failed, e); break;
        case Req::New: e.detail = "session/new"; FailStart(StartState::Failed, e); break;
        case Req::SetMode: RequestFailed("set_mode", e); break;
        case Req::SetConfig: RequestFailed("set_config_option", e); break;
        case Req::Prompt: break;  // never timed
        }
    }

    // ------------------------------------------------------------ notifications
    void OnUpdate(const SJ::Value* params) {
        const SJ::Value* update = Object(params, "update");
        if (!update) return;
        const std::string kind = Str(update, "sessionUpdate", 64);
        {
            std::lock_guard<std::mutex> lock(mu);
            if (!live) {
                // A session that is loading replays its history: dropped. Only the commands are kept.
                if (kind == "available_commands_update") info.commands = ParseCommands(Member(update, "availableCommands"));
                return;
            }
            if (Str(params, "sessionId", 512) != sessionId) return;  // a subagent or a stale session
        }
        Event e;
        if (kind == "agent_message_chunk" || kind == "agent_thought_chunk") {
            const SJ::Value* content = Object(update, "content");
            if (Str(content, "type", 32) != "text") return;
            e.type = kind == "agent_message_chunk" ? Event::Type::MessageChunk : Event::Type::ThoughtChunk;
            e.text = Str(content, "text");
            e.messageId = Str(update, "messageId", 256);
            if (e.text.empty()) return;
        } else if (kind == "tool_call" || kind == "tool_call_update") {
            e.type = kind == "tool_call" ? Event::Type::ToolCall : Event::Type::ToolCallUpdate;
            e.tool = ParseToolCall(update);
            if (e.tool.id.empty()) return;
            if (e.type == Event::Type::ToolCall) {  // a new call always has its kind and status (schema defaults)
                if (!e.tool.hasKind) { e.tool.hasKind = true; e.tool.kind = "other"; }
                if (!e.tool.hasStatus) { e.tool.hasStatus = true; e.tool.status = "pending"; }
            }
        } else if (kind == "plan") {
            e.type = Event::Type::Plan;
            if (const SJ::Value* entries = Array(update, "entries"))
                for (const SJ::Value& entry : entries->items) {
                    if (!entry.IsObject() || e.plan.size() >= 256) continue;
                    e.plan.push_back({Str(&entry, "content", 16u << 10), Str(&entry, "priority", 32), Str(&entry, "status", 32)});
                }
        } else if (kind == "available_commands_update") {
            e.type = Event::Type::CommandsChanged;
            e.commands = ParseCommands(Member(update, "availableCommands"));
            std::lock_guard<std::mutex> lock(mu);
            info.commands = e.commands;
        } else if (kind == "current_mode_update") {
            const std::string mode = Str(update, "currentModeId", 256);
            if (mode.empty()) return;
            bool options;
            {
                std::lock_guard<std::mutex> lock(mu);
                info.modes.currentId = mode;
                options = SyncOptionFromModeLocked();
            }
            EmitSettings(true, options);
            return;
        } else if (kind == "config_option_update") {
            bool modes;
            {
                std::lock_guard<std::mutex> lock(mu);
                info.configOptions = ParseConfigOptions(Member(update, "configOptions"));
                modes = SyncModeFromOptionLocked();
            }
            EmitSettings(modes, true);
            return;
        } else if (kind == "usage_update") {
            e.type = Event::Type::Usage;
            const SJ::Value* used = Member(update, "used");
            const SJ::Value* size = Member(update, "size");
            if (!used || !used->IsNumber() || !size || !size->IsNumber()) return;
            e.usage.used = used->integral ? used->integer : std::int64_t(used->number);
            e.usage.size = size->integral ? size->integer : std::int64_t(size->number);
            if (const SJ::Value* cost = Object(update, "cost")) {
                const SJ::Value* amount = cost->Get("amount");
                if (amount && amount->IsNumber()) {
                    e.usage.hasCost = true;
                    e.usage.costAmount = amount->number;
                    e.usage.costCurrency = Str(cost, "currency", 16);
                }
            }
        } else if (kind == "session_info_update") {
            e.type = Event::Type::SessionTitle;
            e.text = Str(update, "title", 1024);
            if (e.text.empty()) return;
        } else if (kind == "notice") {
            e.type = Event::Type::Notice;
            e.text = Str(update, "title", 4096);
            e.severity = Str(update, "severity", 32);
            if (e.text.empty()) return;
        } else {
            return;  // user_message_chunk, compaction, anything newer: not shown
        }
        Emit(std::move(e));
    }

    // Extension of Claude's adapter: which login the agent process itself uses, pushed soon after initialize and
    // when it changes. Only the kind is kept (never the account's e-mail or organisation).
    void OnAuthStatus(const SJ::Value* params) {
        const std::string kind = Str(Object(params, "authStatus"), "kind", 32);
        if (kind.empty()) return;
        Event e;
        e.type = Event::Type::AuthStatus;
        e.text = kind;
        Emit(std::move(e));
    }

    // ------------------------------------------------------------ requests from the agent
    void EmitQuestion(Ask& ask, const std::string& baseId) {  // mu held
        const FormQuestion& q = ask.questions[ask.index];
        ask.current = baseId;
        Event e;
        e.type = Event::Type::PermissionRequest;
        e.permission.requestId = baseId;
        e.permission.question = true;
        const std::string said = Trim(ask.message);
        std::string title = !q.description.empty() ? q.description : !said.empty() ? said : q.title;
        e.permission.toolCall.id = "question:" + q.id;
        e.permission.toolCall.hasTitle = true;
        e.permission.toolCall.title = title;
        e.permission.toolCall.hasKind = true;
        e.permission.toolCall.kind = "question";
        if (!said.empty() && said != title && ask.questions.size() == 1) e.permission.message = said;
        // Skip first; its label is the integration's (catalog), so the name stays empty.
        e.permission.options.push_back({"skip", "", "reject_once", ""});
        for (std::size_t i = 0; i < q.choices.size(); ++i)
            e.permission.options.push_back({std::to_string(i), q.choices[i].name, "answer", q.choices[i].description});
        pendingEmits.push_back(std::move(e));
    }
    std::vector<Event> pendingEmits;  // mu held while filled; flushed after unlock
    void FlushEmits() {
        std::vector<Event> list;
        {
            std::lock_guard<std::mutex> lock(mu);
            list.swap(pendingEmits);
        }
        for (auto& e : list) Emit(std::move(e));
    }
    void OnAgentRequest(const std::string& method, const SJ::Value& id, const SJ::Value* params) {
        if (method == "session/request_permission") {
            Event e;
            e.type = Event::Type::PermissionRequest;
            {
                std::lock_guard<std::mutex> lock(mu);
                // Not ours, or the turn is being cancelled: the answer is "cancelled".
                if (!live || cancelRequested || Str(params, "sessionId", 512) != sessionId) {
                    SendLocked(Reply(id, PermissionOutcome("")));
                    cv.notify_all();
                    return;
                }
                e.permission.requestId = "p" + std::to_string(++askSeq);
                Ask ask;
                ask.rpcId = id;
                asks.emplace(e.permission.requestId, std::move(ask));
            }
            e.permission.toolCall = ParseToolCall(Object(params, "toolCall"));
            if (const SJ::Value* options = Array(params, "options"))
                for (const SJ::Value& o : options->items) {
                    if (!o.IsObject() || e.permission.options.size() >= 16) continue;
                    PermissionOption option{Str(&o, "optionId", 256), Str(&o, "name", 1024), Str(&o, "kind", 32), ""};
                    if (!option.id.empty()) e.permission.options.push_back(std::move(option));
                }
            Emit(std::move(e));
            return;
        }
        if (method == "elicitation/create" && opts.questions) {
            std::vector<FormQuestion> questions;
            const bool form = Str(params, "mode", 32) == "form" && FormQuestions(Object(params, "requestedSchema"), questions);
            {
                std::lock_guard<std::mutex> lock(mu);
                if (!form || !live) {
                    SendLocked(Reply(id, Action(live ? "decline" : "cancel")));
                } else if (cancelRequested) {
                    SendLocked(Reply(id, Action("cancel")));
                } else {
                    const std::string requestId = "q" + std::to_string(++askSeq);
                    Ask ask;
                    ask.rpcId = id;
                    ask.question = true;
                    ask.questions = std::move(questions);
                    ask.message = Str(params, "message", 4096);
                    auto it = asks.emplace(requestId, std::move(ask)).first;
                    EmitQuestion(it->second, requestId);
                }
            }
            cv.notify_all();
            FlushEmits();
            return;
        }
        Send(ReplyError(id, -32601, "Method not found"));  // i18n-exempt: JSON-RPC standard error message
    }
    // Every request still on screen is answered "cancelled" (when the agent can still hear it) and dismissed.
    void DismissAsks(bool answer = false) {
        std::vector<std::string> ids;
        {
            std::lock_guard<std::mutex> lock(mu);
            for (auto& kv : asks) {
                ids.push_back(kv.second.question ? kv.second.current : kv.first);
                if (answer) SendLocked(Reply(kv.second.rpcId, kv.second.question ? Action("cancel") : PermissionOutcome("")));
            }
            asks.clear();
        }
        if (answer) cv.notify_all();
        for (const auto& id : ids) {
            Event e;
            e.type = Event::Type::PermissionDismissed;
            e.permission.requestId = id;
            Emit(std::move(e));
        }
    }

    // ------------------------------------------------------------ inbound lines
    void OnLine(const std::string& line) {
        SJ::Value msg;
        std::string why;
        if (!SJ::Parse(line, msg, &why, LineLimits()) || !msg.IsObject()) {
            Warn("invalid_json: " + (why.empty() ? std::string("not an object") : why));  // i18n-exempt: developer detail
            return;
        }
        const SJ::Value* method = msg.Get("method");
        const SJ::Value* id = msg.Get("id");
        if (method && method->IsString()) {
            const SJ::Value* params = msg.Get("params");
            if (id && !id->IsNull()) OnAgentRequest(method->string, *id, params);
            else if (method->string == "session/update") OnUpdate(params);
            else if (method->string == "_auth/status_update") OnAuthStatus(params);
            return;  // other notifications are ignored
        }
        if (id && id->IsNumber() && id->integral && (msg.Get("result") || msg.Get("error"))) {
            OnResponse(id->integer, msg.Get("result"), msg.Get("error"));
            return;
        }
        Warn("unexpected_message");
    }
    void ReadLoop() {
        std::string buffer;
        std::vector<char> chunk(64u << 10);
        bool discarding = false;  // inside a line longer than MaxLineBytes
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(stdoutR, chunk.data(), DWORD(chunk.size()), &got, nullptr) || got == 0) break;
            std::size_t start = 0;
            for (std::size_t i = 0; i < got; ++i) {
                if (chunk[i] != '\n') continue;
                if (!discarding) {
                    buffer.append(chunk.data() + start, i - start);
                    if (!buffer.empty() && buffer.back() == '\r') buffer.pop_back();
                    if (!buffer.empty()) OnLine(buffer);
                }
                buffer.clear();
                discarding = false;
                start = i + 1;
            }
            if (discarding) continue;
            buffer.append(chunk.data() + start, got - start);
            if (buffer.size() > MaxLineBytes) {
                buffer.clear();
                buffer.shrink_to_fit();
                discarding = true;
                Warn("line_too_long");
            }
        }
    }
    void ErrLoop() {
        char chunk[4096];
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(stderrR, chunk, sizeof chunk, &got, nullptr) || got == 0) break;
            std::lock_guard<std::mutex> lock(errMu);
            stderrTail.append(chunk, got);
            if (stderrTail.size() > MaxDetailBytes) stderrTail.erase(0, stderrTail.size() - MaxDetailBytes);
        }
    }
    // Writer and timer: the agent may read slowly (a 3 MB prompt through a 64 KiB pipe), never on the caller's thread.
    void PumpLoop() {
        std::unique_lock<std::mutex> lock(mu);
        for (;;) {
            if (done) return;
            const auto now = Clock::now();
            std::vector<Pending> expired;
            auto next = Clock::time_point::max();
            for (auto it = pending.begin(); it != pending.end();) {
                if (it->second.timed && it->second.deadline <= now) {
                    expired.push_back(std::move(it->second));
                    it = pending.erase(it);
                } else {
                    if (it->second.timed) next = std::min(next, it->second.deadline);
                    ++it;
                }
            }
            if (!expired.empty()) {
                lock.unlock();
                for (const auto& p : expired) OnTimeout(p);
                lock.lock();
                continue;
            }
            if (!outgoing.empty() && stdinW) {
                std::string line = std::move(outgoing.front());
                outgoing.pop_front();
                const HANDLE out = stdinW;
                lock.unlock();
                bool ok = true;
                for (std::size_t off = 0; ok && off < line.size();) {
                    DWORD put = 0;
                    ok = WriteFile(out, line.data() + off, DWORD(std::min<std::size_t>(line.size() - off, 1u << 20)), &put, nullptr) && put;
                    off += put;
                }
                lock.lock();
                if (!ok) outgoing.clear();  // the agent is gone; the reader reports it
                continue;
            }
            if (closeInput && stdinW) {
                CloseHandle(stdinW);  // end of input: a well-behaved agent exits by itself
                stdinW = nullptr;
                continue;
            }
            if (next == Clock::time_point::max()) cv.wait(lock);
            else cv.wait_until(lock, next);
        }
    }

    // ------------------------------------------------------------ process
    bool Spawn(const LaunchSpec& spec, Error& error) {
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        HANDLE inR = nullptr, outW = nullptr, errW = nullptr;
        auto fail = [&](const char* api) {
            error.code = ErrorCode::SpawnFailed;
            error.detail = LastErrorText(api, GetLastError());
            for (HANDLE* h : {&inR, &outW, &errW, &stdinW, &stdoutR, &stderrR, &job})
                if (*h) {
                    CloseHandle(*h);
                    *h = nullptr;
                }
            return false;
        };
        if (!CreatePipe(&inR, &stdinW, &sa, 0) || !CreatePipe(&stdoutR, &outW, &sa, 1u << 16) || !CreatePipe(&stderrR, &errW, &sa, 0))
            return fail("CreatePipe");
        // Our ends are never inherited.
        SetHandleInformation(stdinW, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stdoutR, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stderrR, HANDLE_FLAG_INHERIT, 0);
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) return fail("CreateJobObject");
        // Only the three pipe ends are inherited, never another inheritable handle of the browser.
        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
        std::vector<unsigned char> attrBuffer(attrSize);
        auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
        HANDLE inherit[3] = {inR, outW, errW};
        if (!attrSize || !InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) return fail("InitializeProcThreadAttributeList");
        if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr)) {
            DeleteProcThreadAttributeList(attrs);
            return fail("UpdateProcThreadAttribute");
        }
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof si;
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = inR;
        si.StartupInfo.hStdOutput = outW;
        si.StartupInfo.hStdError = errW;
        si.lpAttributeList = attrs;
        std::wstring cmd = QuoteArgument(spec.exe);
        for (const auto& a : spec.args) cmd += L" " + QuoteArgument(a);
        std::wstring block = BuildEnvironmentBlock(spec);
        const std::wstring cwd = spec.cwd.empty() ? Widen(opts.cwd) : spec.cwd;
        PROCESS_INFORMATION pi{};
        // Suspended until it is in the job: nothing it starts can escape the kill.
        const BOOL started = CreateProcessW(spec.exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                                            block.data(), cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
        const DWORD createError = GetLastError();
        DeleteProcThreadAttributeList(attrs);
        if (!started) {
            SetLastError(createError);
            return fail("CreateProcess");
        }
        if (!AssignProcessToJobObject(job, pi.hProcess)) {
            const DWORD assignError = GetLastError();
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            SetLastError(assignError);
            return fail("AssignProcessToJobObject");
        }
        CloseHandle(inR);
        CloseHandle(outW);
        CloseHandle(errW);
        {
            std::lock_guard<std::mutex> lock(mu);
            process = pi.hProcess;
        }
        pid = pi.dwProcessId;
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        return true;
    }

    void Main() {
        Resolution resolved;
        try {
            ResolveContext ctx;
            ctx.signIn = signIn;
            resolved = opts.resolve ? opts.resolve(ctx) : Resolution();
            if (!opts.resolve) resolved.error.code = ErrorCode::NotReady;
        } catch (...) {
            resolved.error.code = ErrorCode::SpawnFailed;
            resolved.error.detail = "resolver";
        }
        bool stopped;
        {
            std::lock_guard<std::mutex> lock(mu);
            stopped = stopRequested;
        }
        if (stopped) {
            Finish();
            return;
        }
        if (!resolved.error.Ok()) {
            const ErrorCode c = resolved.error.code;
            FailStart(c == ErrorCode::NotInstalled || c == ErrorCode::CliTooOld ? StartState::NotInstalled
                      : c == ErrorCode::NeedsPrepare ? StartState::NeedsPrepare : StartState::Failed,
                      resolved.error);
            Finish();
            return;
        }
        Error spawnError;
        if (!Spawn(resolved.launch, spawnError)) {
            FailStart(StartState::Failed, spawnError);
            Finish();
            return;
        }
        {
            // Stopped while the process was being created: nobody else will kill it.
            std::lock_guard<std::mutex> lock(mu);
            if (stopRequested) TerminateJobObject(job, 1);
        }
        auto self = shared_from_this();
        errThread = std::thread([self] { self->ErrLoop(); });
        pump = std::thread([self] { self->PumpLoop(); });
        SendInitialize();
        ReadLoop();

        // End of the agent's output: it exited, or it is being stopped.
        std::thread kill;
        bool wasStopping, answered, prompting;
        {
            std::lock_guard<std::mutex> lock(mu);
            wasStopping = stopRequested;
            answered = startAnswered;
            prompting = phase == Phase::Prompting;
            stopRequested = true;
            closeInput = true;
            phase = Phase::Stopped;
            kill = std::move(killer);
        }
        cv.notify_all();
        if (kill.joinable()) kill.join();
        DWORD code = DWORD(-1);
        if (WaitForSingleObject(process, wasStopping ? 0 : 1000) == WAIT_OBJECT_0) GetExitCodeProcess(process, &code);
        TerminateJobObject(job, 1);  // whatever the agent left behind
        WaitForSingleObject(process, 2000);  // "stopped" means gone, not "being killed"
        {
            std::lock_guard<std::mutex> lock(mu);
            done = true;
        }
        cv.notify_all();
        if (pump.joinable()) pump.join();
        if (errThread.joinable()) errThread.join();
        if (!wasStopping) {
            Error e;
            e.code = ErrorCode::ProcessExited;
            e.detail = Clip(StderrTail(), MaxDetailBytes);
            if (!answered) {
                FailStart(StartState::Failed, e);
            } else {
                DismissAsks();
                if (prompting) {
                    Event turn;
                    turn.type = Event::Type::TurnEnded;
                    turn.error = e;
                    Emit(std::move(turn));
                }
                Event exited;
                exited.type = Event::Type::Exited;
                exited.exitCode = int(code);
                exited.error = e;
                Emit(std::move(exited));
            }
        }
        Finish();
    }
    void Finish() {
        std::thread kill;
        {
            std::lock_guard<std::mutex> lock(mu);
            phase = Phase::Stopped;
            stopRequested = true;
            quiet = true;
            kill = std::move(killer);
        }
        if (kill.joinable()) kill.join();
        for (HANDLE* h : {&stdinW, &stdoutR, &stderrR, &process, &job})
            if (*h) {
                CloseHandle(*h);
                *h = nullptr;
            }
        pid = 0;
        SetEvent(stoppedEvent);
    }
};

Session::Session() : impl_(std::make_shared<SessionImpl>()) {}
Session::~Session() { Dispose(); }

void Session::SetWake(std::function<void()> wake) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->wake = std::move(wake);
}

bool Session::Start(StartOptions options) {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (impl_->run) {
            std::lock_guard<std::mutex> runLock(impl_->run->mu);
            if (impl_->run->phase != Phase::Stopped) return false;
        }
        run = std::make_shared<Run>();
        run->owner = impl_;
        run->generation = ++impl_->generation;
        run->signIn = !options.authenticateMethodId.empty();
        run->opts = options;
        impl_->options = std::move(options);
        impl_->options.authenticateMethodId.clear();
        impl_->hasOptions = true;
        impl_->run = run;
    }
    std::thread([run] { run->Main(); }).detach();
    return true;
}

bool Session::Authenticate(const std::string& methodId) {
    StartOptions options;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (!impl_->hasOptions || methodId.empty()) return false;
        for (const AuthMethod& m : impl_->authMethods)
            if (m.id == methodId && m.terminal) return false;
        options = impl_->options;
    }
    Dispose();
    options.authenticateMethodId = methodId;
    return Start(std::move(options));
}

bool Session::Prompt(std::vector<ContentBlock> blocks) {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        run = impl_->run;
    }
    if (!run) return false;
    bool images;
    {
        std::lock_guard<std::mutex> lock(run->mu);
        if (run->phase != Phase::Ready) return false;
        images = run->info.capabilities.image;
    }
    SJ::Value prompt = SJ::Value::Arr();
    for (ContentBlock& b : blocks) {
        SJ::Value block = SJ::Value::Obj();
        switch (b.kind) {
        case ContentBlock::Kind::Text:
            block.Set("type", SJ::Value::Str("text"));
            block.Set("text", SJ::Value::Str(std::move(b.text)));
            break;
        case ContentBlock::Kind::Image:
            if (!images) continue;
            block.Set("type", SJ::Value::Str("image"));
            block.Set("mimeType", SJ::Value::Str(std::move(b.mimeType)));
            block.Set("data", SJ::Value::Str(std::move(b.data)));
            break;
        case ContentBlock::Kind::ResourceLink:
            block.Set("type", SJ::Value::Str("resource_link"));
            block.Set("uri", SJ::Value::Str(std::move(b.uri)));
            block.Set("name", SJ::Value::Str(std::move(b.name)));
            if (!b.title.empty()) block.Set("title", SJ::Value::Str(std::move(b.title)));
            if (!b.mimeType.empty()) block.Set("mimeType", SJ::Value::Str(std::move(b.mimeType)));
            break;
        }
        prompt.Push(std::move(block));
    }
    {
        std::lock_guard<std::mutex> lock(run->mu);
        if (run->phase != Phase::Ready) return false;
        SJ::Value params = SJ::Value::Obj();
        params.Set("sessionId", SJ::Value::Str(run->sessionId));
        params.Set("prompt", std::move(prompt));
        run->phase = Phase::Prompting;
        run->cancelRequested = false;
        run->RequestLocked(Req::Prompt, "session/prompt", std::move(params), 0);
    }
    run->cv.notify_all();
    return true;
}

void Session::Cancel() {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        run = impl_->run;
    }
    if (!run) return;
    {
        std::lock_guard<std::mutex> lock(run->mu);
        if (run->phase != Phase::Prompting || run->cancelRequested) return;
        run->cancelRequested = true;
    }
    run->DismissAsks(true);  // the client answers "cancelled" to every pending request before the notification
    SJ::Value params = SJ::Value::Obj();
    {
        std::lock_guard<std::mutex> lock(run->mu);
        params.Set("sessionId", SJ::Value::Str(run->sessionId));
    }
    SJ::Value m = SJ::Value::Obj();
    m.Set("jsonrpc", SJ::Value::Str("2.0"));
    m.Set("method", SJ::Value::Str("session/cancel"));
    m.Set("params", std::move(params));
    run->Send(m);
}

bool Session::SetMode(const std::string& modeId) {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        run = impl_->run;
    }
    if (!run || modeId.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(run->mu);
        if (run->phase != Phase::Ready && run->phase != Phase::Prompting) return false;
        SJ::Value params = SJ::Value::Obj();
        params.Set("sessionId", SJ::Value::Str(run->sessionId));
        params.Set("modeId", SJ::Value::Str(modeId));
        run->RequestLocked(Req::SetMode, "session/set_mode", std::move(params), run->opts.timeouts.settingMs, modeId);
    }
    run->cv.notify_all();
    return true;
}

bool Session::SetConfigOption(const std::string& configId, const std::string& value) {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        run = impl_->run;
    }
    if (!run || configId.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(run->mu);
        if (run->phase != Phase::Ready && run->phase != Phase::Prompting) return false;
        bool boolean = false;
        for (const auto& o : run->info.configOptions)
            if (o.id == configId) boolean = o.type == "boolean";
        SJ::Value params = SJ::Value::Obj();
        params.Set("sessionId", SJ::Value::Str(run->sessionId));
        params.Set("configId", SJ::Value::Str(configId));
        if (boolean) {
            params.Set("type", SJ::Value::Str("boolean"));
            params.Set("value", SJ::Value::Bool(value == "true"));
        } else {
            params.Set("value", SJ::Value::Str(value));
        }
        run->RequestLocked(Req::SetConfig, "session/set_config_option", std::move(params), run->opts.timeouts.settingMs, configId, value);
    }
    run->cv.notify_all();
    return true;
}

bool Session::RespondPermission(const std::string& requestId, const std::string& optionId) {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        run = impl_->run;
    }
    if (!run) return false;
    {
        std::lock_guard<std::mutex> lock(run->mu);
        auto it = run->asks.find(requestId);
        if (it == run->asks.end()) {
            // A later question of a form is shown under "<form id>:<index>".
            const std::size_t colon = requestId.find(':');
            if (colon != std::string::npos) it = run->asks.find(requestId.substr(0, colon));
            if (it == run->asks.end() || !it->second.question || it->second.current != requestId) return false;
        } else if (it->second.question && it->second.current != requestId) {
            return false;
        }
        Ask& ask = it->second;
        if (!ask.question) {
            run->SendLocked(Run::Reply(ask.rpcId, Run::PermissionOutcome(optionId)));
            run->asks.erase(it);
        } else {
            const FormQuestion& q = ask.questions[ask.index];
            std::size_t choice = q.choices.size();
            if (!optionId.empty() && optionId != "skip" && optionId.size() <= 3 && optionId.find_first_not_of("0123456789") == std::string::npos)
                choice = std::size_t(std::stoul(optionId));
            if (optionId.empty()) {
                run->SendLocked(Run::Reply(ask.rpcId, Run::Action("cancel")));
                run->asks.erase(it);
            } else if (choice >= q.choices.size()) {  // "skip" or an unknown answer: the agent carries on without it
                run->SendLocked(Run::Reply(ask.rpcId, Run::Action("decline")));
                run->asks.erase(it);
            } else {
                ask.answers.emplace_back(q.id, q.choices[choice].value);
                if (++ask.index < ask.questions.size()) {
                    run->EmitQuestion(ask, it->first + ":" + std::to_string(ask.index));
                } else {
                    SJ::Value content = SJ::Value::Obj();
                    for (const auto& a : ask.answers) content.Set(a.first, SJ::Value::Str(a.second));
                    SJ::Value result = Run::Action("accept");
                    result.Set("content", std::move(content));
                    run->SendLocked(Run::Reply(ask.rpcId, std::move(result)));
                    run->asks.erase(it);
                }
            }
        }
    }
    run->cv.notify_all();
    run->FlushEmits();
    return true;
}

void Session::Dispose() {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        run = impl_->run;
        ++impl_->generation;  // nothing from the old run is delivered any more
        impl_->queue.clear();
    }
    if (run) run->Stop(true);
}

bool Session::WaitStopped(unsigned timeoutMs) {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        run = impl_->run;
    }
    return !run || WaitForSingleObject(run->stoppedEvent, timeoutMs) == WAIT_OBJECT_0;
}

std::size_t Session::Poll(std::vector<Event>& out) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    const std::size_t n = impl_->queue.size();
    for (auto& e : impl_->queue) out.push_back(std::move(e));
    impl_->queue.clear();
    return n;
}

bool Session::WaitForEvents(unsigned timeoutMs) {
    std::unique_lock<std::mutex> lock(impl_->mu);
    return impl_->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return !impl_->queue.empty(); });
}

Phase Session::phase() const {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        run = impl_->run;
    }
    if (!run) return Phase::Idle;
    std::lock_guard<std::mutex> lock(run->mu);
    return run->phase;
}

unsigned long Session::processId() const {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        run = impl_->run;
    }
    return run ? run->pid.load() : 0;
}

}  // namespace Acp
