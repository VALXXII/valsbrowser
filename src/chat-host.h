#pragma once
// Chat intégré — browser integration (design/chat-integration.md). Included once, at the end of
// src/annotation-mode.h (main.cpp's translation unit), after namespace Ann: it uses the annotation mode's picker,
// capture, store and job queue, and the host's globals.
// Contracts: design/annotations-chat-v1-brief.md — A (src/acp-client.h, src/agent-runtime.h), B (design/chat-panel.md,
// assets/chat-panel/), C (src/change-snapshots.h), D (src/chat-prompt.h).
//
// « Au repos = rien » : until the panel is opened for the first time nothing here exists — no WebView, no window,
// no thread, no file read, no agent process. Opening creates the panel's WebView2 (same environment as the tabs);
// closing hides it at once and destroys it PanelIdleMs later; the agent's process is stopped AgentIdleMs after its
// last turn (PanelIdleMs after the panel closed) and resumed (session/resume) at the next message.
//
//   open      mode Annotation only: band button « Demander à l'agent », Ctrl+Maj+D (also starts the mode), or
//             « Demander à l'agent » in the note panel of a selection. The page SHRINKS by the panel's width (ContentBox),
//             the panel slides in from the right (220 ms, the band's curve), out in 160 ms; reduced motion: no slide.
//             The annotation launcher hides while the panel is open.
//   sites     local only (src/chat-local.h); elsewhere the panel opens disabled, with the reason.
//   folder    a site is linked to its project folder once (detected: the process listening on the site's port ->
//             its working folder; else the Windows folder picker), remembered per origin (src/chat-store.h).
//   aim       the mode's own picker: with the panel open the next click goes to the chat (a chip in the composer)
//             instead of the note panel; file:line from Astro / React debug data, checked against the project folder.
//   turn      message.user -> annotations written and claimed -> ChangeSnapshots::BeginTurn -> Acp::Session::Prompt ->
//             events streamed to the panel -> EndTurn (changes, checkpoint) -> annotations resolved / released ->
//             the page reloads when files changed and no dev server does it -> the element gets its « done » mark.
#include <deque>
#include "chat-local.h"
#include "chat-prompt.h"
#include "chat-tools.h"
#include "chat-batch.h"
#include "chat-options.h"
#include "chat-bubble.h"
#include <set>
#include "chat-store.h"
#include "chat-folder.h"
#include "chat-panel-view.h"
#include "acp-client.h"
#include "agent-runtime.h"
#include "change-snapshots.h"

namespace Chat {
namespace SJ = StrictJson; namespace BA = BrowserAnnotations; namespace AH = AnnotationHost; namespace AUI = NativeChrome::Annotations;
namespace CS = ChangeSnapshots;

// ---------------------------------------------------------------- small helpers
inline constexpr float PanelWidthDip = 380, PanelMinDip = 320, PageMinDip = 320;
inline constexpr int SlideInMs = 220, SlideOutMs = 160;
unsigned EnvMs(const wchar_t* name, unsigned fallback) { const std::wstring v = Ann::EnvVar(name); if (v.empty()) return fallback; const unsigned long n = wcstoul(v.c_str(), nullptr, 10); return n >= 50 && n <= 3600000 ? unsigned(n) : fallback; }
unsigned PanelIdleMs() { static const unsigned v = EnvMs(L"BROWSER_CHAT_PANEL_IDLE_MS", 60000); return v; }    // closed panel -> WebView destroyed
unsigned AgentIdleMs() { static const unsigned v = EnvMs(L"BROWSER_CHAT_AGENT_IDLE_MS", 600000); return v; }   // no turn -> agent stopped
bool TestMode() { return !Ann::EnvVar(L"BROWSER_ANNOTATIONS_TEST").empty(); }
void Diag(const std::wstring& s) { Log(L"CHAT " + s); }   // log lines (English diagnostics): the i18n gate knows « Diag »
std::string U8(const std::wstring& w) { return AH::Utf8(w); }
std::wstring W(const std::string& s) { return AH::Wide(s); }
std::string T8(I18n::Id id) { return I18n::U8(id); }
SJ::Value Str(const std::string& s) { return SJ::Value::Str(s); }
SJ::Value Str(const std::wstring& s) { return SJ::Value::Str(U8(s)); }
std::string Base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string out; out.reserve((in.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const unsigned b0 = static_cast<unsigned char>(in[i]), b1 = i + 1 < in.size() ? static_cast<unsigned char>(in[i + 1]) : 0, b2 = i + 2 < in.size() ? static_cast<unsigned char>(in[i + 2]) : 0;
        out += t[b0 >> 2]; out += t[((b0 & 3) << 4) | (b1 >> 4)]; out += i + 1 < in.size() ? t[((b1 & 15) << 2) | (b2 >> 6)] : '='; out += i + 2 < in.size() ? t[b2 & 63] : '=';
    }
    return out;
}
std::string RandomHex(std::size_t chars) {
    std::string token = BA::NewClaimToken(), out;   // "clm_" + 26 base32 chars from BCryptGenRandom
    unsigned acc = 0;
    for (std::size_t i = 4; i < token.size() && out.size() < chars; ++i) { acc = acc * 131 + static_cast<unsigned char>(token[i]); out += "0123456789abcdef"[(static_cast<unsigned char>(token[i]) * 7 + acc) & 15]; }
    while (out.size() < chars) out += '0';
    return out;
}
std::string NewId(const char* prefix) { return std::string(prefix) + RandomHex(12); }
// ISO 8601 with the local offset ("2026-11-17T15:34:00+01:00").
std::string Iso(std::int64_t unix) {
    ULARGE_INTEGER u; u.QuadPart = ULONGLONG(unix + 11644473600LL) * 10000000ULL; FILETIME ft{u.LowPart, u.HighPart}; SYSTEMTIME utc{}, loc{};
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &loc)) return "";
    FILETIME lf{}; SystemTimeToFileTime(&loc, &lf); ULARGE_INTEGER l{}; l.LowPart = lf.dwLowDateTime; l.HighPart = lf.dwHighDateTime;
    const long long offMin = (static_cast<long long>(l.QuadPart) - static_cast<long long>(u.QuadPart)) / 600000000LL;
    char b[40]; snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d%c%02lld:%02lld", loc.wYear, loc.wMonth, loc.wDay, loc.wHour, loc.wMinute, loc.wSecond, offMin < 0 ? '-' : '+', std::llabs(offMin) / 60, std::llabs(offMin) % 60);
    return b;
}
std::string ClipUtf8(const std::string& s, std::size_t maxBytes) {
    if (s.size() <= maxBytes) return s; std::size_t n = maxBytes; while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n; return s.substr(0, n);
}

// ---------------------------------------------------------------- agents (contract A: runtime)
// Ids are AgentRuntime::AgentId ("claude" | "codex"); `actor` is the name written in the annotation store
// (claimed by / resolved by): the history shows « Claude » / « Codex » from it.
struct AgentDef { const char* id; const char* name; const char* icon; const char* actor; AgentRuntime::Agent rt; };
inline constexpr AgentDef Agents[] = {{"claude", "Claude Code", "claude", "browser-chat-claude-code", AgentRuntime::Agent::Claude},
                                      {"codex", "Codex", "codex", "browser-chat-codex", AgentRuntime::Agent::Codex}};
const AgentDef* AgentOf(const std::string& id) { for (const auto& a : Agents) if (id == a.id) return &a; return nullptr; }
std::wstring AgentShort(const std::string& id) { return id == "codex" ? L"Codex" : L"Claude"; }
// Everything the runtime looks up outside (CLI, <data>gents, the test hook BROWSER_CHAT_FAKE_AGENT). Reads the
// registry: built off the UI thread, or once per panel opening.
AgentRuntime::Environment RtEnv() { return AgentRuntime::Environment::FromProcess(Ann::DataRoot()); }
struct AgentInfo {   // what the panel shows for one agent (state.agents)
    std::string status = "starting"; std::wstring detail; double progress = -1; bool cli = false, prepared = false, known = false;
    std::vector<Acp::AuthMethod> auth; std::vector<Acp::ConfigOption> options; Acp::ModeState modes; Acp::Capabilities caps; std::vector<Acp::Command> commands;
};
std::map<std::string, AgentInfo> agentInfo;

// ---------------------------------------------------------------- state
enum class PanelState { Closed, Opening, Open, Closing };
struct Element {   // an aimed element: a chip in the composer, then part of a turn, then an annotation
    std::string id, label, kind, selector, text, hint, thumb; ChatPrompt::SourceRef source, nearest; std::vector<std::string> components;
    BA::Annotation pending; AH::Selection sel; std::string png; double shotScale = 1; std::string shotCrop;
    std::uint64_t serial = 0; bool captureDone = false, sourceDone = false, attached = false; ChatPrompt::PageSource rawSource;   // as the page said it (resolved against the folder)
    std::string annSite, annId, token;   // once written and claimed
    bool closeResolved = false; std::string closeSummary;   // how to close it when its write comes back after the turn ended
    std::string anchorFor;   // Mode Commentaire: aimed as the element of this conversation's bubble (kept for its messages, never a chip)
};
struct Attachment { std::string id, name, mime, base64, thumb; };
struct Queued { std::string id, text; std::vector<std::string> elements, attachments; std::string batchId; std::vector<std::string> annotations; std::string convId; };   // convId (Mode Commentaire): the conversation it belongs to, "" = the engine's   // annotations: a batch « Traiter N annotations » that waits its turn
struct Problem { std::wstring title, message; bool retry = true, connect = false, newChat = false; };
struct Turn {
    bool active = false, cancelled = false, snapshot = false, promptSent = false, ending = false;
    bool batch = false;   // « Traiter les annotations »: the prompt is the batch's instruction, the card follows our tools
    std::string id, text, snapshotTurn, lastMessageId, thoughtId, summaryText; double startedMs = 0, thoughtStart = 0; int messageSeq = 0, thoughtSeq = 0;
    std::vector<Element> elements; std::vector<Attachment> attachments; std::uint64_t navStamp = 0;
    std::map<std::string, std::size_t> itemOf;   // message / thought / tool id -> index in convo.items
    std::string markSelector; std::string planPermission, planId; std::vector<Acp::PlanEntry> lastPlan; std::vector<Acp::PermissionOption> planOptions;
    Acp::Error failure; Problem problem;   // why it failed (an agent error code, or our own sentence)
    struct OurCall { ChatTools::Ours tool = ChatTools::Ours::None; std::string annotation, input; int count = -1; bool done = false, reported = false; };   // input: its arguments (an update does not repeat them)
    std::map<std::string, OurCall> ourCalls;   // tool call id -> one of our annotation tools (its step line is ours to word)
};
struct Convo { ChatStore::Conversation data; int turnSeq = 0; bool preambleSent = false; };
// « Traiter les annotations » (contract B 1.4): one pending annotation of the site, as a line of the block / the card.
struct BatchItem {
    std::string id, siteKey, note, element, source, thumb; std::string status = "pending", text;   // text: summary (done) or reason (skipped)
    bool claimedHere = false;   // this turn's agent took it (its own claim call succeeded): ours to give back if it stops there
};
struct Batch {
    std::string id; std::vector<BatchItem> items;
    std::string instruction; std::int64_t sentAt = 0;                 // once sent: the exact text the agent got, and when (store seconds)
    std::string conversation, userId; std::size_t item = std::size_t(-1);   // the user's message that carries the card: which conversation, which item
};
ChatPanelView::View view; PanelState panelState = PanelState::Closed; bool panelReady = false, uiInit = false, picking = false;
HWND post = nullptr; constexpr UINT AcpWake = WM_APP + 21; constexpr UINT_PTR DestroyTimer = 1, AgentIdleTimer = 2, RearmTimer = 3, MarkTimer = 4, FolderTimer = 5, HoverTimer = 6;
AUI::Ramp slide; std::weak_ptr<Tab> chatTab; ChatLocal::Site site; std::wstring folder; ChatFolder::Guess suggestion; bool suggestionAsked = false;
// Project folder without a screen (contract B 1.4): `folderAsk` = the detection is uncertain, the panel shows « Où est
// le code de ce site ? »; `folderChosen` = the current folder was picked or confirmed by the user (never replaced by a
// detection); `agentChosen` = the agent comes from the user or from this site's last use (else: the first installed).
bool folderAsk = false, folderChosen = false, agentChosen = false;
std::wstring folderInstead;   // with folderChosen: the folder the detection proposed when the user chose another one
std::string folderMarksOrigin; int folderMarksTries = 0;   // the page's file marks are asked once the mode's script is in the page
ChatStore::Projects projects; bool projectsLoaded = false; std::string agentId = "claude";
// Mode Commentaire (chat-comment.h): while the agent works on one element's conversation (convo, « the engine's »), the
// bubble of another element may show its own conversation « aside »: the engine's thread messages are then not sent.
struct Aside { bool on = false; Convo c; };
Convo convo; Aside aside; bool asideSending = false; Turn turn; std::deque<Queued> queue; std::map<std::string, Element> composerElements; std::map<std::string, Attachment> composerAttachments;
std::unique_ptr<Acp::Session> session; bool sessionStarting = false, sessionReady = false, preparing = false; Acp::StartResult started;
std::string pendingModel, pendingEffort, pendingMode; std::function<void()> afterReady; std::function<int()> preparePermille;
std::vector<std::unique_ptr<Acp::Session>> disposed;   // stopped sessions whose threads may still be ending (Shutdown waits for them)
std::vector<Element> orphanAnnotations;                // annotations of a turn that ended before their write came back
std::map<std::uint64_t, Element> aiming;                // elements being aimed (by capture serial): image and source still coming
std::uint64_t turnNavStamp = 0; int rearmTries = 0; std::function<void()> afterTurn; std::function<void(bool)> afterUndo;
std::map<std::string, SJ::Value> waitingPermissions, activeStrips;   // what the panel must show again when its WebView is (re)created: cards still waiting, strips still true
Batch composerBatch, activeBatch;   // posed in the composer (not sent yet) / sent: the card of the current or last batch turn
bool batchOwed = false;             // « Traiter avec l'agent » asked before the panel was ready
std::map<std::string, std::string> annotationNotes;   // annotation id -> its note, as seen in this session (turns, tool results): the « … » of a step line
std::wstring pendingAsk;                               // « Demander à l'agent » from the note: the text to send once the element is attached
std::shared_ptr<CS::Store> snapshots; std::map<std::string, std::string> checkpointOfTurn;   // panel turn id -> checkpoint id
std::map<std::string, std::string> snapshotOfTurn;                                              // panel turn id -> snapshot turn id
bool rearmOwed = false; std::string doneMarkSelector; int elementSeq = 0; int pendingAnnotations = 0;
struct OpenStats { double requestedAt = 0, createdAt = 0, readyAt = 0, firstTextAt = 0, sendAt = 0, sessionAt = 0; } stats;
void FinishTurn(const std::string& stopReason);

std::wstring ProjectsFile() { return Ann::DataRoot() + L"\\annotations-chat\\projects.json"; }
ChatStore::History& History() { static ChatStore::History h(Ann::DataRoot()); return h; }
std::shared_ptr<Tab> ChatTab() { return chatTab.lock(); }
bool Open() { return panelState == PanelState::Opening || panelState == PanelState::Open; }
bool Shown() { return panelState != PanelState::Closed; }
bool Usable() { return site.local && !site.file && !folder.empty(); }
const AgentDef& Agent() { const auto* a = AgentOf(agentId); return a ? *a : Agents[0]; }
float PanelDip() { return std::min(PanelWidthDip, std::max(PanelMinDip, chrome.width - PageMinDip)); }
float InsetDip() { return Shown() && !InFullscreen() && !PageFullscreenActive() ? PanelDip() : 0.f; }

// ---------------------------------------------------------------- host -> panel
void OnHiddenPermission();
bool ThreadMessage(const char* type, const SJ::Value& msg) {
    static const char* list[] = {"message.user", "turn.start", "message.agentChunk", "thought", "toolCall", "toolCallUpdate", "plan", "permission.request", "permission.cancel",
                                 "turn.end", "turn.undone", "annotations.status", "changes.diff", "edit.confirm", "usage", "annotation.kept"};
    for (const char* x : list) if (strcmp(x, type) == 0) return true;
    if (strcmp(type, "error") == 0) { const auto* sc = msg.Get("scope"); return !sc || !sc->IsString() || sc->string != "composer"; }
    return false;
}
void Send(const char* type, SJ::Value msg = SJ::Value::Obj()) {
    if (!panelReady) return;
    if (aside.on && !asideSending && ThreadMessage(type, msg)) { if (strcmp(type, "permission.request") == 0) OnHiddenPermission(); return; }   // Mode Commentaire: another conversation is shown
    SJ::Value out = SJ::Value::Obj(); out.Set("v", SJ::Value::Int(1)); out.Set("type", Str(std::string(type)));
    for (auto& m : msg.members) out.Set(m.first, std::move(m.second));
    view.Post(SJ::Serialize(out));
}
// A finished item is kept for the history (contract B §4.2) and sent live.
SJ::Value Item(const char* type, SJ::Value fields) { SJ::Value v = SJ::Value::Obj(); v.Set("type", Str(std::string(type))); for (auto& m : fields.members) v.Set(m.first, std::move(m.second)); return v; }
std::size_t Keep(const char* type, const SJ::Value& fields) { convo.data.items.push_back(Item(type, fields)); return convo.data.items.size() - 1; }
SJ::Value ElementRef(const Element& e) {
    SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(e.id)); o.Set("label", Str(e.label)); o.Set("kind", Str(e.kind));
    if (!e.selector.empty()) o.Set("selector", Str(e.selector));
    if (e.source.known) o.Set("source", Str(e.source.Display()));
    if (!e.text.empty()) o.Set("text", Str(e.text));
    if (!e.hint.empty()) o.Set("hint", Str(e.hint));
    if (!e.thumb.empty()) o.Set("thumb", Str(e.thumb));
    return o;
}
SJ::Value AttachmentRef(const Attachment& a) { SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(a.id)); o.Set("kind", Str(std::string("image"))); o.Set("name", Str(a.name)); o.Set("thumb", Str(a.thumb)); return o; }

// The agent's permission modes as the panel's three choices. The lists come from the session (nothing is hard-coded
// here); which of the agent's ids stand for « Demander / Auto / Planifier » comes from AgentRuntime::GetModeMap (checked
// against the real adapters), with the id's wording as a fallback for an agent the map does not know.
//   - a mode that asks for nothing at all (ModeMap::never) is not offered;
//   - « Planifier » may be a session mode (Claude) or a config option (Codex: planOption = planOn / planOff): the
//     panel sees ONE list; PlanOptionId is the entry that stands for the option.
inline constexpr const char* PlanOptionId = "plan.option";
struct ModeEntry { std::string id, name, description, kind; };
struct ModeView { std::vector<ModeEntry> list; std::string current; bool viaOption = false; std::string optionId; AgentRuntime::ModeMap map; };
bool In(const std::vector<std::string>& l, const std::string& v) { return std::find(l.begin(), l.end(), v) != l.end(); }
std::string ModeKind(const AgentRuntime::ModeMap& map, const std::string& id, const std::string& name) {
    if (In(map.ask, id)) return "ask";
    if (In(map.automatic, id)) return "auto";
    if (!map.planMode.empty() && id == map.planMode) return "plan";
    if (id == PlanOptionId) return "plan";
    std::string l; for (char c : id + " " + name) l += char(std::tolower(static_cast<unsigned char>(c)));
    if (l.find("plan") != std::string::npos) return "plan";
    if (l.find("auto") != std::string::npos || l.find("accept") != std::string::npos || l.find("agent") != std::string::npos) return "auto";
    if (l.find("default") != std::string::npos || l.find("ask") != std::string::npos || l.find("read") != std::string::npos) return "ask";
    return "other";
}
ModeView ModesOf(const std::string& agent, const AgentInfo& info) {
    ModeView m; if (const auto* def = AgentOf(agent)) m.map = AgentRuntime::GetModeMap(def->rt);
    auto add = [&](const std::string& id, const std::string& name, const std::string& description) { if (!In(m.map.never, id)) m.list.push_back({id, name, description, ModeKind(m.map, id, name)}); };
    if (!info.modes.available.empty()) { for (const auto& x : info.modes.available) add(x.id, x.name, x.description); m.current = info.modes.currentId; }
    else if (const auto* o = Acp::FindModeOption(info.options)) { m.viaOption = true; m.optionId = o->id; m.current = o->currentValue; for (const auto& c : o->choices) add(c.value, c.name, c.description); }
    if (!m.map.planOption.empty()) for (const auto& o : info.options) if (o.id == m.map.planOption) for (const auto& c : o.choices) if (c.value == m.map.planOn) {
        m.list.push_back({PlanOptionId, c.name.empty() ? c.value : c.name, c.description, "plan"}); if (o.currentValue == m.map.planOn) m.current = PlanOptionId; }
    return m;
}
// V1.4: the modes are named in the user's language, as in Paper 14RK-0 (« Demander », « Auto », « Planifier »), never
// with the adapter's English labels (« Manual », « Accept edits »…). Each agent has two « auto » modes: the first of its
// map (file edits accepted: Claude « acceptEdits », Codex « workspace-write ») is « Auto »; the one that decides alone
// (Claude « auto », Codex « agent ») is « Autonome ». A mode the map does not know keeps the agent's own name.
bool Autonomous(const ModeEntry& m) { return m.kind == "auto" && (m.id == "auto" || m.id == "agent"); }
void NameMode(ModeEntry& m, const ModeView& v) {
    (void)v; const bool codex = agentId == "codex";
    if (m.kind == "ask") { m.name = T8(I18n::Id::chat_mode_ask); m.description = T8(I18n::Id::chat_mode_ask_desc); }
    else if (m.kind == "plan") { m.name = T8(I18n::Id::chat_mode_plan); m.description = T8(I18n::Id::chat_mode_plan_desc); }
    else if (Autonomous(m)) { m.name = T8(I18n::Id::chat_mode_autonomous); m.description = codex ? T8(I18n::Id::chat_mode_autonomous_codex_desc) : U8(TRF(chat_mode_autonomous_desc, W(std::string(Agent().name)))); }
    else if (m.kind == "auto") { m.name = T8(I18n::Id::chat_mode_auto); m.description = T8(codex ? I18n::Id::chat_mode_auto_workspace_desc : I18n::Id::chat_mode_auto_desc); }
}
int ModeRank(const ModeEntry& m, const ModeView& v) { (void)v; return m.kind == "ask" ? 0 : m.kind == "auto" && !Autonomous(m) ? 1 : m.kind == "plan" ? 2 : Autonomous(m) ? 3 : 4; }
std::string KindOfMode(const ModeView& m, const std::string& id) { for (const auto& e : m.list) if (e.id == id) return e.kind; return "other"; }
std::map<std::string, std::string> agentFirstModel;   // agent id -> the model it had chosen itself when its first session started (its « Par défaut » when it has no alias)
// An agent's list as the panel shows it (V1.2): never the « use my default » alias; `defaultValue` gets « Par défaut ».
SJ::Value OptionList(const Acp::ConfigOption* o, const std::string& defaultValue = "") {
    SJ::Value arr = SJ::Value::Arr(); if (!o) return arr;
    for (const auto& c : o->choices) { if (!ChatStore::ValidId(c.value) || ChatOptions::IsAlias(c)) continue; SJ::Value v = SJ::Value::Obj(); v.Set("id", Str(c.value)); v.Set("label", Str(c.name.empty() ? c.value : c.name));
        if (!c.description.empty()) v.Set("description", Str(ClipUtf8(c.description, 300))); if (!c.group.empty()) v.Set("group", Str(c.group));
        if (!defaultValue.empty() && c.value == defaultValue) v.Set("isDefault", SJ::Value::Bool(true)); arr.Push(std::move(v)); }
    return arr;
}
std::string DefaultModelOf(const std::string& id, const Acp::ConfigOption* o) { if (!o) return ""; auto it = agentFirstModel.find(id); return ChatOptions::DefaultModel(o->choices, it == agentFirstModel.end() ? std::string() : it->second); }
// The current choice's name; an alias is named by the model it stands for (models) or not at all (efforts: no « Default »).
std::string LabelOf(const Acp::ConfigOption* o, const std::string& agent = "") {
    if (!o) return "";
    const std::string shown = ChatOptions::Shown(o->choices, o->currentValue, DefaultModelOf(agent, o));
    for (const auto& c : o->choices) if (c.value == shown) return ChatOptions::IsAlias(c) ? std::string() : c.name.empty() ? c.value : c.name;
    return ChatOptions::IsAlias(shown, shown) ? std::string() : shown;
}
void SendState();
void SetAgentStatus(const std::string& id, const std::string& status, const std::wstring& detail = L"", double progress = -1) {
    auto& a = agentInfo[id]; a.status = status; a.detail = detail; a.progress = progress; SendState();
}
int CountPending();
void SendState() {
    if (!panelReady) return;
    SJ::Value st = SJ::Value::Obj();
    {   SJ::Value s = SJ::Value::Obj(); s.Set("origin", Str(site.origin)); s.Set("label", Str(site.file ? T8(I18n::Id::chat_site_file) : site.label)); s.Set("local", SJ::Value::Bool(site.local)); st.Set("site", std::move(s)); }
    auto folderObj = [](const std::wstring& path) { SJ::Value f = SJ::Value::Obj(); f.Set("path", Str(path)); f.Set("display", Str(ChatFolder::Display(path))); f.Set("name", Str(ChatFolder::Name(path))); return f; };
    const bool usableSite = site.local && !site.file;
    // not a local site: no « where is the code » step (null would show it) — an empty folder, and disabledReason says why
    st.Set("folder", !usableSite ? folderObj(L"") : folder.empty() ? SJ::Value::Null() : folderObj(folder));
    if (usableSite && folder.empty() && !suggestion.folder.empty()) { SJ::Value f = folderObj(suggestion.folder); f.Set("reason", Str(std::string("auto"))); st.Set("folderSuggestion", std::move(f)); }
    else st.Set("folderSuggestion", SJ::Value::Null());
    st.Set("askFolder", SJ::Value::Bool(usableSite && folder.empty() && folderAsk));
    SJ::Value agents = SJ::Value::Arr();
    for (const auto& def : Agents) {
        const auto& info = agentInfo[def.id]; SJ::Value a = SJ::Value::Obj();
        a.Set("id", Str(std::string(def.id))); a.Set("name", Str(std::string(def.name))); a.Set("icon", Str(std::string(def.icon))); a.Set("status", Str(info.status));
        if (!info.detail.empty()) a.Set("detail", Str(info.detail));
        if (info.progress >= 0) a.Set("progress", SJ::Value::Num(std::clamp(info.progress, 0.0, 1.0)));
        if (!info.auth.empty()) { SJ::Value ms = SJ::Value::Arr(); for (const auto& m : info.auth) if (ChatStore::ValidId(m.id)) { SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(m.id)); o.Set("label", Str(m.name.empty() ? m.id : m.name)); ms.Push(std::move(o)); } a.Set("authMethods", std::move(ms)); }
        {   const auto* mo = Acp::FindModelOption(info.options); a.Set("models", OptionList(mo, DefaultModelOf(def.id, mo))); }
        agents.Push(std::move(a));
    }
    st.Set("agents", std::move(agents)); st.Set("agentId", Str(agentId));
    const auto& info = agentInfo[agentId];
    const auto* model = Acp::FindModelOption(info.options); const auto* effort = Acp::FindEffortOption(info.options);
    {   const std::string shown = model ? ChatOptions::Shown(model->choices, model->currentValue, DefaultModelOf(agentId, model)) : std::string();
        st.Set("model", ChatStore::ValidId(shown) ? Str(shown) : SJ::Value::Null()); }
    // efforts (V1.2): the agent's levels without its « Default »; a session still on that alias shows no effort
    st.Set("efforts", OptionList(effort)); st.Set("effort", effort && ChatStore::ValidId(effort->currentValue) && !ChatOptions::IsAlias(effort->currentValue, effort->currentValue) ? Str(effort->currentValue) : SJ::Value::Null());
    {   const auto mv = ModesOf(agentId, info); SJ::Value modes = SJ::Value::Arr();
        std::vector<ModeEntry> shown = mv.list; for (auto& m : shown) NameMode(m, mv);
        std::stable_sort(shown.begin(), shown.end(), [&](const ModeEntry& a, const ModeEntry& b) { return ModeRank(a, mv) < ModeRank(b, mv); });
        for (const auto& m : shown) { if (!ChatStore::ValidId(m.id)) continue; SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(m.id)); o.Set("label", Str(m.name.empty() ? m.id : m.name));
            if (!m.description.empty()) o.Set("description", Str(ClipUtf8(m.description, 300))); o.Set("kind", Str(m.kind)); modes.Push(std::move(o)); }
        st.Set("modes", std::move(modes)); st.Set("mode", ChatStore::ValidId(mv.current) ? Str(mv.current) : SJ::Value::Null()); }
    {   const auto& shown = aside.on ? aside.c.data.head : convo.data.head; const bool running = turn.active && !aside.on;   // contract B 1.8: the conversation shown
        SJ::Value c = SJ::Value::Obj(); c.Set("id", Str(shown.id)); c.Set("title", Str(shown.title.empty() ? T8(I18n::Id::chat_title_new) : shown.title));
        if (!shown.source.empty()) c.Set("source", Str(shown.source)); c.Set("running", SJ::Value::Bool(running)); st.Set("conversation", std::move(c));
        st.Set("running", SJ::Value::Bool(running)); }
    st.Set("pendingAnnotations", SJ::Value::Int(pendingAnnotations));
    {   SJ::Value sg = SJ::Value::Arr();
        if (Usable()) {
            if (pendingAnnotations > 0) { SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(std::string("annotations"))); o.Set("label", Str(pendingAnnotations == 1 ? T8(I18n::Id::chat_suggest_annotations_one) : U8(TRF(chat_suggest_annotations_other, std::to_wstring(pendingAnnotations))))); o.Set("badge", Str(std::to_string(pendingAnnotations))); sg.Push(std::move(o)); }
            for (const auto& s : {std::pair<const char*, I18n::Id>{"mobile", I18n::Id::chat_suggest_mobile}, {"contrast", I18n::Id::chat_suggest_contrast}}) { SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(std::string(s.first))); o.Set("label", Str(T8(s.second))); sg.Push(std::move(o)); }
        }
        st.Set("suggestions", std::move(sg)); }
    {   SJ::Value c = SJ::Value::Obj(); c.Set("image", SJ::Value::Bool(info.caps.image || !sessionReady)); c.Set("steering", SJ::Value::Bool(false)); c.Set("commands", SJ::Value::Bool(!info.commands.empty())); st.Set("capabilities", std::move(c)); }
    st.Set("disabledReason", site.local && !site.file ? SJ::Value::Null() : Str(T8(site.file ? I18n::Id::chat_disabled_file : I18n::Id::chat_disabled_not_local)));
    Send("state", std::move(st));
}
void SendCommands() {
    SJ::Value items = SJ::Value::Arr();
    for (const auto& c : agentInfo[agentId].commands) { if (c.name.empty() || c.name.size() > 80) continue; SJ::Value o = SJ::Value::Obj(); o.Set("name", Str(c.name)); o.Set("description", Str(ClipUtf8(c.description, 300))); items.Push(std::move(o)); }
    SJ::Value m = SJ::Value::Obj(); m.Set("items", std::move(items)); Send("commands", std::move(m));
}
void SendQueue() {
    SJ::Value items = SJ::Value::Arr();
    const std::string shown = aside.on ? aside.c.data.head.id : convo.data.head.id;
    for (const auto& q : queue) {
        if ((q.convId.empty() ? convo.data.head.id : q.convId) != shown) continue;   // Mode Commentaire: the messages of the conversation shown
        SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(q.id));
        if (!q.convId.empty() && q.convId != convo.data.head.id && turn.active) o.Set("after", Str(convo.data.head.anchorLabel.empty() ? ChatPrompt::OneLine(convo.data.head.title, 40) : convo.data.head.anchorLabel));   // contract B 1.8 §19.6
        if (!q.annotations.empty() && q.text.find_first_not_of(" \t\r\n") == std::string::npos) o.Set("text", Str(TRP(chat_batch_queue, q.annotations.size(), std::to_wstring(q.annotations.size()))));
        else o.Set("text", Str(ClipUtf8(q.text, 400)));
        items.Push(std::move(o));
    }
    SJ::Value m = SJ::Value::Obj(); m.Set("items", std::move(items)); Send("queue", std::move(m));
}
// Context gauge (contract B 1.2): tokens used / window, as the agent last reported them for this conversation.
void SendUsage() {
    const auto& h = aside.on ? aside.c.data.head : convo.data.head; if (h.windowTokens <= 0) return;
    SJ::Value m = SJ::Value::Obj(); m.Set("used", SJ::Value::Int(h.usedTokens)); m.Set("size", SJ::Value::Int(h.windowTokens)); Send("usage", std::move(m));
}
void SendThread() {
    // 4 MiB per message (contract B): thread.reset, then thread.append by slices.
    struct Flag { Flag() { asideSending = aside.on; } ~Flag() { asideSending = false; } } flag;   // the conversation shown, whichever it is
    const auto& shown = aside.on ? aside.c : convo;
    std::size_t i = 0; bool first = true; const auto& items = shown.data.items;
    do {
        SJ::Value arr = SJ::Value::Arr(); std::size_t bytes = 0;
        for (; i < items.size(); ++i) { const std::size_t n = SJ::Serialize(items[i]).size(); if (bytes && bytes + n > (3u << 20)) break; bytes += n; arr.Push(items[i]); }
        SJ::Value m = SJ::Value::Obj(); if (first) m.Set("conversationId", Str(shown.data.head.id)); m.Set("items", std::move(arr));
        Send(first ? "thread.reset" : "thread.append", std::move(m)); first = false;
    } while (i < items.size());
    SendUsage();   // thread.reset hides the gauge: shown again when this conversation's usage is known
}
void Strip(const char* id, const char* tone, const std::wstring& text, const char* actionId = nullptr, const std::wstring& actionLabel = L"") {
    SJ::Value m = SJ::Value::Obj(); m.Set("id", Str(std::string(id))); m.Set("tone", Str(std::string(tone))); m.Set("text", Str(text));
    if (actionId) { SJ::Value a = SJ::Value::Obj(); a.Set("id", Str(std::string(actionId))); a.Set("label", Str(actionLabel)); m.Set("action", std::move(a)); }
    activeStrips[id] = m; Send("strip", std::move(m));
}
void ClearStrip(const char* id) { activeStrips.erase(id); SJ::Value m = SJ::Value::Obj(); m.Set("id", Str(std::string(id))); m.Set("clear", SJ::Value::Bool(true)); Send("strip", std::move(m)); }
void SaveConvo() {
    if (convo.data.head.id.empty() || convo.data.items.empty()) return;
    convo.data.head.updatedAt = BA::SystemNow();
    auto copy = std::make_shared<ChatStore::Conversation>(convo.data);
    void NoteAnchored(const ChatStore::Header& h); NoteAnchored(convo.data.head);   // Mode Commentaire: its element's frame + icon
    Ann::Run([copy] { if (!History().Save(*copy)) OutputDebugStringW(L"chat: conversation not saved\n"); }, nullptr);
}

// ---------------------------------------------------------------- errors (codes -> catalog text)
Problem ProblemFor(const Acp::Error& e, const std::string& agent) {
    const std::wstring name = W(AgentOf(agent) ? AgentOf(agent)->name : "Claude Code"); Problem p;
    switch (e.code) {
    case Acp::ErrorCode::NotInstalled: p.title = TRF(chat_err_not_installed_title, name); p.message = TRF(chat_err_not_installed, name); break;
    case Acp::ErrorCode::CliTooOld: p.title = TRF(chat_err_too_old_title, name); p.message = TRF(chat_err_too_old, name); break;
    case Acp::ErrorCode::NeedsPrepare: p.title = TR(chat_err_prepare_title); p.message = TR(chat_err_prepare); break;
    case Acp::ErrorCode::AuthRequired: p.title = TRF(chat_err_auth_title, name); p.message = TR(chat_err_auth); p.connect = true; p.retry = false; break;
    case Acp::ErrorCode::ProcessExited: case Acp::ErrorCode::SpawnFailed: p.title = TRF(chat_err_exited_title, name); p.message = TR(chat_err_exited); break;
    case Acp::ErrorCode::Timeout: p.title = TRF(chat_err_timeout_title, name); p.message = TR(chat_err_timeout); break;
    case Acp::ErrorCode::ContextTooLong: p.title = TR(chat_err_context_title); p.message = TR(chat_err_context); p.newChat = true; p.retry = false; break;
    case Acp::ErrorCode::UsageLimit: p.title = TR(chat_err_usage_title); p.message = TRF(chat_err_usage, name); break;
    case Acp::ErrorCode::Overloaded: case Acp::ErrorCode::RateLimited: case Acp::ErrorCode::ServerError: case Acp::ErrorCode::Network: p.title = TRF(chat_err_service_title, name); p.message = TR(chat_err_service); break;
    default: p.title = TR(chat_err_generic_title); p.message = TR(chat_err_generic); break;
    }
    return p;
}
void SendError(const Problem& p, const std::string& detail, const char* scope = "thread", bool keep = true) {
    SJ::Value m = SJ::Value::Obj(); m.Set("id", Str(NewId("err_"))); m.Set("scope", Str(std::string(scope))); m.Set("title", Str(p.title)); m.Set("message", Str(p.message));
    if (!detail.empty()) m.Set("detail", Str(ClipUtf8(detail, 4000)));
    SJ::Value actions = SJ::Value::Arr();
    auto add = [&](const char* id, I18n::Id label, bool primary) { SJ::Value a = SJ::Value::Obj(); a.Set("id", Str(std::string(id))); a.Set("label", Str(T8(label))); if (primary) a.Set("primary", SJ::Value::Bool(true)); actions.Push(std::move(a)); };
    if (p.connect) add("connect", I18n::Id::chat_action_connect, true);
    if (p.retry) add("retry", I18n::Id::chat_action_retry, !p.connect);
    if (p.newChat) add("newChat", I18n::Id::chat_title_new, true);
    m.Set("actions", std::move(actions));
    if (turn.active && !turn.id.empty()) m.Set("turnId", Str(turn.id));
    if (keep && std::string(scope) == "thread") Keep("error", m);
    Send("error", std::move(m));
}

void SyncChromeBits();
// Mode Commentaire (chat-comment.h)
bool CommentLayout(); bool BubbleOpen(); void SyncMarks(); void CloseBubble(bool focusPage); void SendLayout(bool openNow); void OnBubbleSize(int wCss, int hCss);
bool TakeCommentSelection(const std::shared_ptr<Tab>& t, std::uint64_t serial, bool captureImpossible); bool KeepAnchorElement(Element& e);
bool SwitchEngineTo(const std::string& id); void AddAnchorElement(std::vector<std::string>& ids); void RefreshWaitStrip(); void ShowEngineWork();
void SwitchLayout(bool comment); void RefreshAnchored(); std::string BubbleSelector(); void ForgetPage(); void PlaceBubble(); std::string ShownId();
} // namespace Chat
#include "chat-host-turn.h"
#include "chat-host-ui.h"
#include "chat-host-batch.h"
#include "chat-comment.h"
