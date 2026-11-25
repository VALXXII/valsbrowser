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
#include <atomic>
#include <thread>
#include <dwmapi.h>
#include <tlhelp32.h>
#include "chat-local.h"
#include "chat-prompt.h"
#include "chat-tools.h"
#include "chat-options.h"
#include "chat-bubble.h"
#if __has_include("chat-transition.h")
#include "chat-transition.h"   // R36 (H2): the page shows the agent's changes with a fade / the values gliding
#define CHAT_HAS_TRANSITION 1
#else
#define CHAT_HAS_TRANSITION 0   // until H2's files are in: a plain reload, no chip peek
#endif
#include <set>
#include "chat-store.h"
#include "chat-folder.h"
#include "chat-panel-view.h"
#include "acp-client.h"
#include "agent-runtime.h"
#include "agent-status.h"   // « Ton agent »: the agents' one status (design/ton-agent-interface.md)
#include "chat-agent-status.h"
#include "ton-agent-entry.h"   // OpenTonAgent: the chat's « Installer » / « Mettre à jour » of a missing agent open the screen
#include "change-snapshots.h"
#include "chat-model-probe.h"   // #128 v2: an agent's model list read before its first chat
#include "chat-known-models.h"   // #128 v2: the models shown until the agent says its own

namespace Chat {
namespace SJ = StrictJson; namespace BA = BrowserAnnotations; namespace AH = AnnotationHost; namespace AUI = NativeChrome::Annotations;
namespace CS = ChangeSnapshots;

// ---------------------------------------------------------------- small helpers
inline constexpr float PanelWidthDip = 380, PanelMinDip = 320, PageMinDip = 320;
// #127 side panel layouts (Paper « Annotation · panneau · 2 chat ouvert à droite » 1YEN-0, « compact · 6 » 1ZIY-0,
// « fenêtre étroite · 7 » 1ZOP-0): the chat is a card 4 DIP from the window's right and bottom edges (radius 8, border
// 1 #262626) and the page stops 4 DIP before it. Designer's two default proposals, Val decides while trying:
//   DrawerOverChat: a drawer opened while the chat is open goes OVER the chat (the chat is back when it closes);
//                   false: the chat moves left of the drawer;
//   ChatOverlayBelow: in a window narrower than this, the chat opens over the page instead of pushing it.
inline constexpr bool DrawerOverChat = NativeChrome::FocusMode::drawerOverChat;   // one rule with Mode Focus
inline constexpr float ChatOverlayBelow = NativeChrome::Sidebar::chatOverlayBelow, CardInset = NativeChrome::FocusMode::chatInset, CardRadius = 8;
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
    bool signedIn = false;   // seen signed in since Browser started (a detection said so, or a session was ready): « plus connecté » is true only then
    std::vector<Acp::AuthMethod> auth; std::vector<Acp::ConfigOption> options; Acp::ModeState modes; Acp::Capabilities caps; std::vector<Acp::Command> commands;
};
std::map<std::string, AgentInfo> agentInfo;
bool panelWarm = false;   // O1: the panel's page created in advance (its agent may start)
bool defaultsApplied = false;   // R28: the browser's model / effort / mode choices are applied to the session (until then: shown as intended)
enum : int { OwedMode = 1, OwedModel = 2, OwedEffort = 4 };
int choicesOwed = 0;              // M / E3: a mode, model or effort chosen « aside » while this session's turn ran — it gets them when its turn ends (GiveOwedChoices)

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
struct Queued { std::string id, text; std::vector<std::string> elements, attachments; std::string convId; };   // convId (Mode Commentaire): the conversation it belongs to, "" = the engine's
struct Problem { std::wstring title, message; bool retry = true, connect = false, newChat = false, chooseFolder = false;
                 bool install = false; };   // « Ton agent »: « Installer » opens the agents' screen (its click there is the consent)
struct Turn {
    bool active = false, cancelled = false, snapshot = false, promptSent = false, ending = false;
    bool planLaunched = false;   // M: « Lancer le plan » accepted the agent's plan in this turn — the mode it goes to is the user's choice (KeepModeAfterPlan)
    bool modeWait = false, modeFixed = false;   // SEC-13: the prompt waits for the session's mode; the wanted mode was asked again once for this turn
    std::string id, text, snapshotTurn, lastMessageId, thoughtId, summaryText; double startedMs = 0, thoughtStart = 0; int messageSeq = 0, thoughtSeq = 0;
    std::vector<Element> elements; std::vector<Attachment> attachments; std::uint64_t navStamp = 0;
    std::map<std::string, std::size_t> itemOf;   // message / thought / tool id -> index in convo.items
    std::size_t startItem = std::size_t(-1);       // D6: its `turn.start` in convo.items (model + effort filled in once known)
    std::string markSelector; std::string planPermission, planId; std::vector<Acp::PlanEntry> lastPlan; std::vector<Acp::PermissionOption> planOptions;
    Acp::Error failure; Problem problem;   // why it failed (an agent error code, or our own sentence)
    struct OurCall { ChatTools::Ours tool = ChatTools::Ours::None; std::string annotation; int count = -1; bool done = false; };
    std::map<std::string, OurCall> ourCalls;   // tool call id -> one of our annotation tools (its step line is ours to word)
    std::map<std::string, std::string> toolKinds;   // tool call id -> its ACP kind (an update may not repeat it)
    std::set<std::string> refusedTools, droppedTools;   // tool call id -> its request was refused by the user / withdrawn unanswered (stop): the step never happened
    std::vector<std::string> agentFiles;            // files the agent's edit / delete / move calls named (relative to the folder): the only ones the turn counts and undoes
};
struct Convo { ChatStore::Conversation data; int turnSeq = 0; bool preambleSent = false; std::string recap; };   // recap (R42): given with the next prompt to the agent that took the chat over
ChatPanelView::View view; PanelState panelState = PanelState::Closed; bool panelReady = false, uiInit = false, picking = false;
HWND post = nullptr; constexpr UINT AcpWake = WM_APP + 21; constexpr UINT_PTR DestroyTimer = 1, AgentIdleTimer = 2, RearmTimer = 3, MarkTimer = 4, FolderTimer = 5, HoverTimer = 6, WarmTimer = 7, StopTimer = 8, HeldTimer = 9, PanelFocusTimer = 10, LimitTimer = 11, ModelReadTimer = 12;
// QA (C1, Q's « c1_prechauffage_retarde »): BROWSER_CHAT_TEST_WARM_DELAY_MS=<ms>, read only with BROWSER_ANNOTATIONS_TEST=1, holds
// back the creation in advance of the bubble's and the panel's WebView by that long (a click lands before the page is ready).
unsigned WarmDelayMs() {
    static const unsigned v = [] {
        wchar_t t[8] = L"", d[16] = L"";
        if (!GetEnvironmentVariableW(L"BROWSER_ANNOTATIONS_TEST", t, 8) || wcscmp(t, L"1") != 0) return 0u;
        return GetEnvironmentVariableW(L"BROWSER_CHAT_TEST_WARM_DELAY_MS", d, 16) ? unsigned(std::max(0, _wtoi(d))) : 0u;
    }();
    return v;
}
// true while the warm-up is held back (the first call starts the delay and arms WarmTimer, which calls the warm-ups again)
bool WarmHeldBack() {
    static double since = 0; static bool said = false;
    const unsigned d = WarmDelayMs(); if (!d) return false;
    if (since == 0) since = Ann::NowMs();
    const double left = d - (Ann::NowMs() - since);
    if (left <= 0) return false;
    if (!said) { said = true; Diag(L"warm-up held back " + std::to_wstring(d) + L" ms (test)"); }   // i18n-exempt: log detail
    if (post) SetTimer(post, WarmTimer, UINT(left) + 1, nullptr);
    return true;
}
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
Convo convo; Aside aside; bool asideSending = false; /* M7: the items of the conversation shown (aside or the engine's) */ const std::vector<SJ::Value>& ShownItems(); void ReleaseHeld(); Turn turn; std::deque<Queued> queue; std::map<std::string, Element> composerElements; std::map<std::string, Attachment> composerAttachments;
std::unique_ptr<Acp::Session> session; bool sessionStarting = false, sessionReady = false, preparing = false; Acp::StartResult started;
std::string pendingModel, pendingEffort, pendingMode; ChatOptions::Asked askedModel, askedEffort, askedMode; /* R46: asked to the session, not confirmed yet */ std::function<void()> afterReady; std::function<int()> preparePermille, prepareStage;
std::string modeAwaited; bool modeRefused = false;   // SEC-13: the mode last asked to the session, until it confirms it; the session refused it (or never answered)
std::vector<std::unique_ptr<Acp::Session>> disposed;   // stopped sessions whose threads may still be ending (Shutdown waits for them)
std::vector<Element> orphanAnnotations;                // annotations of a turn that ended before their write came back
std::map<std::uint64_t, Element> aiming;                // elements being aimed (by capture serial): image and source still coming
std::uint64_t turnNavStamp = 0; int rearmTries = 0; std::function<void()> afterTurn; std::function<void(bool)> afterUndo;
std::map<std::string, SJ::Value> waitingPermissions, activeStrips;   // what the panel must show again when its WebView is (re)created: cards still waiting, strips still true
std::map<std::string, std::string> annotationNotes;   // annotation id -> its note, as seen in this session (turns, tool results): the « … » of a step line
std::shared_ptr<CS::Store> snapshots; std::map<std::string, std::string> checkpointOfTurn;   // panel turn id -> checkpoint id
std::map<std::string, std::string> snapshotOfTurn;                                              // panel turn id -> snapshot turn id
bool rearmOwed = false; std::string doneMarkSelector; int elementSeq = 0;
struct OpenStats { double requestedAt = 0, createdAt = 0, readyAt = 0, firstTextAt = 0, sendAt = 0, sessionAt = 0; } stats;
void FinishTurn(const std::string& stopReason);

// Lot R (decision A): where the chat keeps its conversations, its site -> folder memory and its snapshots. A private
// window has its own folder, erased when it closes (Ann::PrivateData); nothing of it reaches the normal windows' files.
std::wstring ChatRoot() { return privateWindow ? Ann::PrivateData() : Ann::DataRoot(); }
// Never an empty root for a file path: a private window whose folder could not be made has no chat (AskAvailable), and
// what still asks for a path gets one that holds nothing and is never written (the folder is not created).
std::wstring ChatFilesRoot() { const std::wstring r = ChatRoot(); return r.empty() ? Ann::PrivateDir() + L"\\unavailable" : r; }
bool openedByMode = false, quietOpening = false;   // lot R: the panel opened by the mode's start in « Panneau » (chat-host-ui.h, OpenAtModeStart)
std::wstring fileFolder;   // the folder of the local FILE the chat is on (ChatLocal::FolderOfFile); empty on a local project
std::wstring ProjectsFile() { return ChatFilesRoot() + L"\\annotations-chat\\projects.json"; }
std::wstring OptionsCacheFile() { return Ann::DataRoot() + L"\\annotations-chat\\agent-options.json"; }   // R13
ChatStore::History& History() { static ChatStore::History h(ChatFilesRoot()); return h; }
std::shared_ptr<Tab> ChatTab() { return chatTab.lock(); }
bool Open() { return panelState == PanelState::Opening || panelState == PanelState::Open; }
bool Shown() { return panelState != PanelState::Closed; }
bool Usable() { return site.local && !folder.empty(); }
const AgentDef& Agent() { const auto* a = AgentOf(agentId); return a ? *a : Agents[0]; }
float PanelDip() { return std::min(PanelWidthDip, std::max(PanelMinDip, chrome.ClientWidth() - PageMinDip)); }   // the window's width (Mode Focus: chrome.width is the floating panel's)
// The page's room taken by the panel (0 while it is over the page: a narrow window in a side panel layout).
bool OverPage() { return chrome.sidebar && chrome.ClientWidth() < ChatOverlayBelow; }
float InsetDip() { return Shown() && !InFullscreen() && !PageFullscreenActive() && !OverPage() ? PanelDip() + (chrome.sidebar ? CardInset : 0.f) : 0.f; }

// ---------------------------------------------------------------- host -> panel
void OnHiddenPermission();
bool ThreadMessage(const char* type, const SJ::Value& msg) {
    static const char* list[] = {"message.user", "turn.start", "message.agentChunk", "thought", "toolCall", "toolCallUpdate", "plan", "permission.request", "permission.cancel",
                                 "turn.end", "turn.undone", "changes.diff", "edit.confirm", "usage"};
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
// R4 (Paper 14RK-0, updated by the user): exactly four lines, in this order, named to the letter — « Accept edits »
// (Claude acceptEdits, Codex workspace-write), « Auto » (Claude auto, Codex agent: the agent decides its permissions),
// « Demander », « Planifier » (ChatOptions::PaperModes). The mode that bypasses every permission is never offered
// (ModeMap::never), nor any other mode of the adapter. The page gets the kinds "edits", "auto", "ask", "plan".
void NameMode(ModeEntry& m, const ModeView& v) {
    (void)v; const auto slot = ChatOptions::SlotOf(m.kind, m.id); m.kind = ChatOptions::SlotKind(slot);
    if (slot == ChatOptions::ModeSlot::Ask) { m.name = T8(I18n::Id::chat_mode_ask); m.description = T8(I18n::Id::chat_mode_ask_desc); }
    else if (slot == ChatOptions::ModeSlot::Plan) { m.name = T8(I18n::Id::chat_mode_plan); m.description = T8(I18n::Id::chat_mode_plan_desc); }
    else if (slot == ChatOptions::ModeSlot::Auto) { m.name = T8(I18n::Id::chat_mode_auto); m.description = U8(TRF(chat_mode_auto_desc, AgentShort(agentId))); }
    else if (slot == ChatOptions::ModeSlot::Edits) { m.name = T8(I18n::Id::chat_mode_edits); m.description = T8(I18n::Id::chat_mode_edits_desc); }
}
std::vector<ModeEntry> ShownModes(const ModeView& mv) {
    std::vector<std::pair<std::string, std::string>> keys; for (const auto& m : mv.list) keys.push_back({m.kind, m.id});
    std::vector<ModeEntry> out; for (const auto i : ChatOptions::PaperModes(keys)) { ModeEntry e = mv.list[i]; NameMode(e, mv); out.push_back(std::move(e)); }
    return out;
}
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
std::string DefaultModelOf(const std::string& id, const Acp::ConfigOption* o) { if (!o) return ""; auto it = agentFirstModel.find(id); return ChatOptions::DefaultModel(o->choices, it == agentFirstModel.end() ? (ChatKnownModels::IsBuiltIn(id, o) ? o->currentValue : std::string()) : it->second); }   // #128 v2: the built-in list's recommended one
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
void SendState() {
    if (!panelReady) return;
    SJ::Value st = SJ::Value::Obj();
    {   SJ::Value s = SJ::Value::Obj(); s.Set("origin", Str(site.origin)); s.Set("label", Str(site.file ? T8(I18n::Id::chat_site_file) : site.label)); s.Set("local", SJ::Value::Bool(site.local)); st.Set("site", std::move(s)); }
    auto folderObj = [](const std::wstring& path) { SJ::Value f = SJ::Value::Obj(); f.Set("path", Str(path)); f.Set("display", Str(ChatFolder::Display(path))); f.Set("name", Str(ChatFolder::Name(path))); return f; };
    const bool usableSite = site.local;   // a local project, or a local file (lot R: its folder is its site)
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
        // owner decision (24 Nov 2026): never signed in reads « Claude Code n'est pas connecté. »; « Vous n'êtes plus connecté à … » only when it was
        if (info.status == "notConnected") a.Set("notice", Str(info.signedIn ? TRF(chat_agent_signed_out, W(std::string(def.name))) : TRF(chat_agent_never_signed_in, W(std::string(def.name)))));
        if (info.progress >= 0) a.Set("progress", SJ::Value::Num(std::clamp(info.progress, 0.0, 1.0)));
        if (!info.auth.empty()) { SJ::Value ms = SJ::Value::Arr(); for (const auto& m : info.auth) if (ChatStore::ValidId(m.id)) { SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(m.id)); o.Set("label", Str(m.name.empty() ? m.id : m.name)); ms.Push(std::move(o)); } a.Set("authMethods", std::move(ms)); }
        {   const auto* mo = ChatKnownModels::Shown(def.id, info.options, info.known, info.cli); a.Set("models", OptionList(mo, DefaultModelOf(def.id, mo))); }   // #128 v2: never empty for an installed agent
        agents.Push(std::move(a));
    }
    st.Set("agents", std::move(agents)); st.Set("agentId", Str(agentId));
    const auto& info = agentInfo[agentId];
    const auto* model = ChatKnownModels::Shown(agentId, info.options, info.known, info.cli); const auto* effort = Acp::FindEffortOption(info.options);   // #128 v2: its built-in list until it says its own
    std::string WantedEffort(const Acp::ConfigOption* e); std::string WantedMode(const ModeView& mv); std::string NewSessionModel(const Acp::ConfigOption* m); std::string NewSessionEffort(const Acp::ConfigOption* e);
    // M (monkey 0.3.561 #78-#81): a conversation shown aside shows what ITS session will get (the user's choice, else the
    // start value) — never the running session of the other element; the session a choice is owed to shows that choice.
    {   std::string shown = model ? ChatOptions::Shown(model->choices, model->currentValue, DefaultModelOf(agentId, model)) : std::string();
        const std::string wanted = NewSessionModel(model);
        if (model) shown = ChatOptions::ShownWhileAsked(ChatOptions::Intended(shown, wanted, defaultsApplied), askedModel, Ann::NowMs());   // R28, R46
        if (model) shown = ChatOptions::ShownSetting(!aside.on, (choicesOwed & OwedModel) != 0, shown, wanted);   // M / E3
        if (aside.on && !pendingModel.empty()) shown = pendingModel;   // E3: the choice of the conversation shown aside
        st.Set("model", ChatStore::ValidId(shown) ? Str(shown) : SJ::Value::Null()); }
    // efforts (V1.2): the agent's levels without its « Default »; R28: before the browser's choice is applied, that choice
    {   std::string cur = effort && !ChatOptions::IsAlias(effort->currentValue, effort->currentValue) ? effort->currentValue : std::string();
        if (effort) cur = ChatOptions::ShownWhileAsked(ChatOptions::Intended(cur, WantedEffort(effort), defaultsApplied), askedEffort, Ann::NowMs());   // R28, R46
        if (effort) cur = ChatOptions::ShownSetting(!aside.on, (choicesOwed & OwedEffort) != 0, cur, NewSessionEffort(effort));   // M / E3
        st.Set("efforts", OptionList(effort)); st.Set("effort", effort && ChatStore::ValidId(cur) ? Str(cur) : SJ::Value::Null()); }
    {   const auto mv = ModesOf(agentId, info); SJ::Value modes = SJ::Value::Arr();
        const std::vector<ModeEntry> shown = ShownModes(mv);
        {   static std::string said; std::string ids; for (const auto& m : shown) ids += (ids.empty() ? "" : ", ") + m.id + "=" + m.kind;
            if (!shown.empty() && ids != said) { said = ids; Diag(L"modes offered: " + W(ids)); } }   // R4: the QA reads it
        for (const auto& m : shown) { if (!ChatStore::ValidId(m.id)) continue; SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(m.id)); o.Set("label", Str(m.name.empty() ? m.id : m.name));
            if (!m.description.empty()) o.Set("description", Str(ClipUtf8(m.description, 300))); o.Set("kind", Str(m.kind)); modes.Push(std::move(o)); }
        const std::string wantedMode = WantedMode(mv);   // the user's choice (just made, else the site's), else « Demander »
        const std::string mode = aside.on && !pendingMode.empty() ? pendingMode   // E3
                               : ChatOptions::ShownSetting(!aside.on, (choicesOwed & OwedMode) != 0, ChatOptions::ShownWhileAsked(ChatOptions::Intended(mv.current, wantedMode, defaultsApplied), askedMode, Ann::NowMs()), wantedMode);   // R28, R46: never the session's starting mode in between; M / E3: nor another element's session
        {   static std::string saidMode; const bool owed = (choicesOwed & OwedMode) != 0; const std::string now = mode + (aside.on ? " (aside)" : "") + (owed ? " (owed)" : "");   // the QA reads it: what the composer shows, and why
            if (!mode.empty() && now != saidMode) { saidMode = now; Diag(L"mode shown: « " + W(mode) + L" »" + (aside.on ? L" — the conversation shown is aside: what its own session will get" : owed ? L" — chosen while this session's turn ran: given at its end" : L"")); } }   // i18n-exempt: log detail
        st.Set("modes", std::move(modes)); st.Set("mode", ChatStore::ValidId(mode) ? Str(mode) : SJ::Value::Null()); }
    {   const auto& shown = aside.on ? aside.c.data.head : convo.data.head; const bool running = turn.active && !aside.on;   // contract B 1.8: the conversation shown
        SJ::Value c = SJ::Value::Obj(); c.Set("id", Str(shown.id)); c.Set("title", Str(shown.title.empty() ? T8(I18n::Id::chat_title_new) : shown.title));
        if (!shown.source.empty()) c.Set("source", Str(shown.source)); c.Set("running", SJ::Value::Bool(running)); st.Set("conversation", std::move(c));
        st.Set("running", SJ::Value::Bool(running)); }
    {   SJ::Value sg = SJ::Value::Arr();
        if (Usable()) {
            // R24: no « Traiter les N annotations en attente » suggestion
            for (const auto& s : {std::pair<const char*, I18n::Id>{"mobile", I18n::Id::chat_suggest_mobile}, {"contrast", I18n::Id::chat_suggest_contrast}}) { SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(std::string(s.first))); o.Set("label", Str(T8(s.second))); sg.Push(std::move(o)); }
        }
        st.Set("suggestions", std::move(sg)); }
    {   SJ::Value c = SJ::Value::Obj(); c.Set("image", SJ::Value::Bool(info.caps.image || !sessionReady)); c.Set("steering", SJ::Value::Bool(false)); c.Set("commands", SJ::Value::Bool(!info.commands.empty())); st.Set("capabilities", std::move(c)); }
    st.Set("disabledReason", site.local ? SJ::Value::Null() : Str(T8(I18n::Id::chat_disabled_not_local)));
    Send("state", std::move(st));
}
void SendCommands() {
    SJ::Value items = SJ::Value::Arr();
    for (const auto& c : agentInfo[agentId].commands) { if (c.name.empty() || c.name.size() > 80) continue; SJ::Value o = SJ::Value::Obj(); o.Set("name", Str(c.name)); o.Set("description", Str(ClipUtf8(c.description, 300))); items.Push(std::move(o)); }
    SJ::Value m = SJ::Value::Obj(); m.Set("items", std::move(items)); Send("commands", std::move(m));
}
bool HeldFor(const std::string& conv, std::string& text);
void SendQueue() {
    SJ::Value items = SJ::Value::Arr();
    const std::string shown = aside.on ? aside.c.data.head.id : convo.data.head.id;
    for (const auto& q : queue) {
        if ((q.convId.empty() ? convo.data.head.id : q.convId) != shown) continue;   // Mode Commentaire: the messages of the conversation shown
        SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(q.id));
        if (!q.convId.empty() && q.convId != convo.data.head.id && turn.active) o.Set("after", Str(convo.data.head.anchorLabel.empty() ? ChatPrompt::OneLine(convo.data.head.title, 40) : convo.data.head.anchorLabel));   // contract B 1.8 §19.6
        o.Set("text", Str(ClipUtf8(q.text, 400)));
        items.Push(std::move(o));
    }
    {   std::string text;   // queue #70: a message held until its element is ready (image) is shown as waiting, in its bubble
        if (HeldFor(shown, text)) { SJ::Value o = SJ::Value::Obj(); o.Set("id", Str(std::string("held"))); o.Set("text", Str(ClipUtf8(text, 400))); items.Push(std::move(o)); } }
    // lot R (validated 2026-11-24): the bubble shows 2 waiting messages at most; the line that counts the others is worded
    // here, in the interface language (« +{n} en attente »: the page puts the number in)
    SJ::Value m = SJ::Value::Obj(); m.Set("items", std::move(items)); m.Set("more", Str(TRF(chat_queue_more, L"{n}"))); Send("queue", std::move(m));
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
    // R9 (Mode Commentaire): thread.reset wipes the page's live cards. A permission the engine's turn still waits for is
    // not an item of the thread: without this, a bubble closed before the request came and opened again showed only the
    // tool's step (« Edit styles\landing.css ») and the turn waited forever.
    if (!aside.on) for (const auto& w : waitingPermissions) Send("permission.request", w.second);
}
void Strip(const char* id, const char* tone, const std::wstring& text, const char* actionId = nullptr, const std::wstring& actionLabel = L"") {
    SJ::Value m = SJ::Value::Obj(); m.Set("id", Str(std::string(id))); m.Set("tone", Str(std::string(tone))); m.Set("text", Str(text));
    if (actionId) { SJ::Value a = SJ::Value::Obj(); a.Set("id", Str(std::string(actionId))); a.Set("label", Str(actionLabel)); m.Set("action", std::move(a)); }
    activeStrips[id] = m; Send("strip", std::move(m));
}
void ClearStrip(const char* id) { activeStrips.erase(id); SJ::Value m = SJ::Value::Obj(); m.Set("id", Str(std::string(id))); m.Set("clear", SJ::Value::Bool(true)); Send("strip", std::move(m)); }
const std::vector<SJ::Value>& ShownItems() { return aside.on ? aside.c.data.items : convo.data.items; }   // M7
void SaveConvo() {
    if (convo.data.head.id.empty() || convo.data.items.empty()) return;
    convo.data.head.updatedAt = BA::SystemNow();
    auto copy = std::make_shared<ChatStore::Conversation>(convo.data);
    bool SentConvo(const Convo& c); convo.data.head.sent = SentConvo(convo); copy->head.sent = convo.data.head.sent;   // R32: kept with it
    void NoteAnchored(const ChatStore::Header& h); NoteAnchored(convo.data.head);   // Mode Commentaire: its element's frame + icon
    Ann::Run([copy] { if (!History().Save(*copy)) OutputDebugStringW(L"chat: conversation not saved\n"); }, nullptr);
}

// ---------------------------------------------------------------- errors (codes -> catalog text)
Problem ProblemFor(const Acp::Error& e, const std::string& agent) {
    const std::wstring name = W(AgentOf(agent) ? AgentOf(agent)->name : "Claude Code"); Problem p;
    switch (e.code) {
    case Acp::ErrorCode::NotInstalled: p.title = TRF(chat_err_not_installed_title, name); p.message = TRF(chat_err_not_installed, name); p.install = true; p.retry = false; break;
    case Acp::ErrorCode::CliTooOld: p.title = TRF(chat_err_too_old_title, name); p.message = TRF(chat_err_too_old, name); p.install = true; p.retry = false; break;
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
// `turnId`: the turn the card belongs to when it is sent after that turn ended (« Ton agent »: a failure re-checked first).
void SendError(const Problem& p, const std::string& detail, const char* scope = "thread", bool keep = true, const std::string& turnId = "") {
    SJ::Value m = SJ::Value::Obj(); m.Set("id", Str(NewId("err_"))); m.Set("scope", Str(std::string(scope))); m.Set("title", Str(p.title)); m.Set("message", Str(p.message));
    if (!detail.empty()) m.Set("detail", Str(ClipUtf8(detail, 4000)));
    SJ::Value actions = SJ::Value::Arr();
    auto add = [&](const char* id, I18n::Id label, bool primary) { SJ::Value a = SJ::Value::Obj(); a.Set("id", Str(std::string(id))); a.Set("label", Str(T8(label))); if (primary) a.Set("primary", SJ::Value::Bool(true)); actions.Push(std::move(a)); };
    if (p.connect) add("connect", I18n::Id::chat_action_connect, true);
    if (p.retry) add("retry", I18n::Id::chat_action_retry, !p.connect);
    if (p.newChat) add("newChat", I18n::Id::chat_title_new, true);
    if (p.chooseFolder) add("chooseFolder", I18n::Id::chat_action_choose_folder, true);
    if (p.install) add("install", I18n::Id::ton_agent_row_install, true);
    m.Set("actions", std::move(actions));
    if (!turnId.empty()) m.Set("turnId", Str(turnId)); else if (turn.active && !turn.id.empty()) m.Set("turnId", Str(turn.id));
    if (keep && std::string(scope) == "thread") Keep("error", m);
    Send("error", std::move(m));
}

void SyncChromeBits();
// Mode Commentaire (chat-comment.h)
bool CommentLayout(); bool BubbleOpen(); bool BubbleWarm(); void SyncMarks(); void CloseBubble(bool focusPage); void SendLayout(bool openNow); void OnBubbleSize(int wCss, int hCss, ChatBubble::Edges over, bool resized);
bool TakeCommentSelection(const std::shared_ptr<Tab>& t, std::uint64_t serial, bool captureImpossible); bool CommentFirst(const std::shared_ptr<Tab>& t); bool KeepAnchorElement(Element& e);
bool SwitchEngineTo(const std::string& id); void AddAnchorElement(std::vector<std::string>& ids); void RefreshWaitStrip(); void ShowEngineWork();
void SwitchLayout(bool comment); void RefreshAnchored(); std::string BubbleSelector(); void ForgetPage(); void PlaceBubble(bool scrolling = false); std::string ShownId();
} // namespace Chat
#include "chat-host-turn.h"
#include "chat-host-ui.h"
#include "chat-comment.h"
