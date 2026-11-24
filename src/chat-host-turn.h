#pragma once
// Chat intégré — agent session and turns (included by src/chat-host.h; see its header for the whole picture).
namespace Chat {

void ProcessQueue(); bool TakeShownAside(); void AttachQueuedAnchor(Queued& q); void ApplyMark(const char* state, const std::string& selector); void AfterTurnPage(const std::vector<std::string>& paths); void ArmAgentIdle(); void StopTurn();
void StartTurn(const std::string& text, const std::vector<std::string>& elementIds, const std::vector<std::string>& attachmentIds, bool batch = false);
// « Traiter les annotations » (chat-host-batch.h)
SJ::Value BatchItemJson(const BatchItem& i, bool withStatus); void BatchToolDone(ChatTools::Ours tool, const std::string& annotation, const std::string& rawInput); void BatchTurnEnded(const std::string& stopReason);
void StartBatchTurn(const std::string& precision, const std::string& batchId, const std::vector<std::string>& ids); void PoseBatch(const wchar_t* from); void ShowAnnotation(const std::string& id);
void DisposeSession(const wchar_t* why);
void SendPrompt(); void ModeSettled(); void ModeRefused(const Acp::Event& e);   // SEC-13

// ---------------------------------------------------------------- « Ton agent »: the agents' one status (src/agent-status.h)
// Started once with the browser (ChatAgentsStartup, main) — or on the first need of the chat. Everyone reads it.
void EnsureAgentStatus() {
    static bool started = false; if (started) return; started = true;
    AgentStatus::Host h;
    h.dataRoot = Ann::DataRoot();
    h.run = [](std::function<void()> work, std::function<void()> done) { Ann::Run(std::move(work), std::move(done)); };
    h.log = [](const std::wstring& s) { Diag(s); };
    h.environment = [] { return RtEnv(); };
    AgentStatus::Init(std::move(h));
}

// ---------------------------------------------------------------- detection and preparation (off the UI thread)
void ApplyDetection(const std::string& id, const AgentRuntime::Detection& d) {
    auto& a = agentInfo[id]; a.known = true; a.cli = d.Usable(); a.prepared = !d.NeedsPrepare();
    if (sessionReady && id == agentId) return;   // a live session says more than a detection
    if (sessionStarting && id == agentId) return;
    if (d.cli.state == AgentRuntime::CliState::Missing) { a.status = "notInstalled"; a.detail = TR(chat_agent_not_installed); }
    else if (d.cli.state == AgentRuntime::CliState::TooOld) { a.status = "error"; a.detail = TR(chat_agent_too_old); }
    else if (d.login == AgentRuntime::Login::SignedOut) { a.status = "notConnected"; a.detail = TR(chat_agent_not_connected); }
    else { a.status = "ready"; a.detail = a.prepared ? TR(chat_agent_installed) : TR(chat_agent_first_use); }
    a.progress = -1;
}
void DetectAgents(std::function<void()> then = nullptr) {
    auto out = std::make_shared<std::vector<AgentRuntime::Detection>>();
    Ann::Run([out] { const auto env = RtEnv(); for (const auto& def : Agents) out->push_back(AgentRuntime::Detect(env, def.rt)); },
             [out, then] {
                 for (std::size_t i = 0; i < out->size() && i < std::size(Agents); ++i) {
                     const auto& d = (*out)[i]; ApplyDetection(Agents[i].id, d);
                     Diag(std::wstring(L"agent ") + W(Agents[i].id) + L": cli " + (d.Usable() ? L"found " : L"missing ") + W(d.cli.version) + L" (" + W(d.cli.source) + L"), node " + (d.nodeReady ? L"ready" : L"absent") + L", adapter " + (d.adapterReady ? L"ready" : L"absent"));
                 }
                 // The agent is chosen without a screen: this site's last one or the user's pick; else Claude Code when it
                 // is installed; else the first agent that is.
                 if (!agentChosen && !turn.active && convo.data.items.empty() && !session && !agentInfo[agentId].cli)
                     for (const auto& def : Agents) if (agentInfo[def.id].cli) { agentId = def.id; convo.data.head.agentId = agentId; Diag(std::wstring(L"agent chosen by itself (the only one installed): ") + W(def.id)); break; }
                 SendState(); if (then) then();
             });
}
void StartSession();
// Downloads the private Node and the adapter (pinned fingerprints), with the progress in the panel, then starts.
std::shared_ptr<std::atomic<bool>> prepareCancel;
void PrepareAgent(const std::string& id, std::function<void(bool)> then) {
    const auto* def = AgentOf(id); if (!def || preparing) { if (then) then(false); return; }
    preparing = true; prepareCancel = std::make_shared<std::atomic<bool>>(false);
    // no bar until something really moves: « Préparation de Claude… » alone (never an empty grey bar)
    SetAgentStatus(id, "preparing", TRF(chat_agent_prepare_start, AgentShort(id)), -1);
    struct Shared { std::atomic<int> permille{0}, stage{0}; AgentRuntime::PrepareResult result; };
    auto shared = std::make_shared<Shared>(); const auto rt = def->rt; auto cancel = prepareCancel; const HWND target = post;
    Ann::Run([shared, rt, cancel, target] {
            const auto env = RtEnv();
            shared->result = AgentRuntime::Prepare(env, rt, [&](const AgentRuntime::Progress& p) {
                // each stage its own label and its own progress (PrepareProgress): Node's download (bytes), then its
                // unpacking (no measure: no bar), then the adapter's packages (count, and the bytes of the current one)
                using St = AgentRuntime::Progress::Stage;
                double f = -1; int stage = 0;
                if (p.stage == St::Node) {
                    if (p.total && p.received >= p.total) stage = 2;   // downloaded: unpacked and checked now
                    else { stage = 1; if (p.total) f = double(p.received) / double(p.total); }
                } else if (p.stage == St::Adapter) {
                    stage = 3;
                    if (p.itemCount) f = (double(p.item) + (p.total ? std::min(1.0, double(p.received) / double(p.total)) : 0.0)) / double(p.itemCount);
                } else if (p.stage == St::Done) stage = 4;
                shared->stage = stage; shared->permille = f < 0 ? -1 : int(std::min(1.0, f) * 1000);
                if (target) PostMessageW(target, WM_APP + 22, 0, 0);
            }, cancel.get());
        },
        [shared, id, then] {
            preparing = false;
            const bool ok = shared->result.Ok();
            Diag(L"prepare " + W(id) + L": " + W(AgentRuntime::PrepareErrorName(shared->result.error)) + (shared->result.detail.empty() ? L"" : L" — " + W(shared->result.detail)));
            auto& a = agentInfo[id]; a.prepared = ok; a.progress = -1;
            if (ok) { a.status = "ready"; a.detail = TR(chat_agent_installed); }
            else if (shared->result.error == AgentRuntime::PrepareError::Cancelled) { a.status = "ready"; a.detail = TR(chat_agent_first_use); }
            else { a.status = "error"; a.detail = TR(chat_agent_unavailable);
                if (!turn.active) { Acp::Error e; e.code = Acp::ErrorCode::NeedsPrepare; SendError(ProblemFor(e, id), std::string(AgentRuntime::PrepareErrorName(shared->result.error)) + " " + shared->result.detail); } }   // a waiting turn says it itself
            SendState(); if (then) then(ok);
        });
    preparePermille = [shared] { return shared->permille.load(); }; prepareStage = [shared] { return shared->stage.load(); };
}
void PrepareProgress() {
    if (!preparing || !preparePermille) return;
    const int p = preparePermille(), stage = prepareStage ? prepareStage() : 0; auto& a = agentInfo[agentId]; const std::wstring who = AgentShort(agentId);
    // a bar only from 1 %: below, the label alone (an empty bar says nothing)
    const double bar = p >= 10 ? p / 1000.0 : -1; const std::wstring pct = std::to_wstring(std::max(0, p) / 10);
    a.status = "preparing"; a.progress = -1;
    switch (stage) {
    case 1: a.detail = bar < 0 ? TR(chat_agent_node_connect) : TRF(chat_agent_node_download, pct); a.progress = bar; break;
    case 2: a.detail = TR(chat_agent_node_install); break;
    case 3: a.detail = bar < 0 ? TRF(chat_agent_adapter_start, who) : TRF(chat_agent_adapter_install, who, pct); a.progress = bar; break;
    case 4: a.detail = TRF(chat_agent_launching, who); break;
    default: a.detail = TRF(chat_agent_prepare_start, who); break;
    }
    SendState();
}

// ---------------------------------------------------------------- session (one agent process per conversation)
void DisposeSession(const wchar_t* why) {
    defaultsApplied = false;   // R28: the next session shows the browser's choices until they are applied
    choicesOwed = 0;           // M: owed to the session that leaves — the next one gets the choices at its start
    askedModel = askedEffort = askedMode = ChatOptions::Asked{};   // R46
    modeAwaited.clear(); modeRefused = false;   // SEC-13: the next session's mode is asked again at its start
    if (!session) return;
    session->Dispose(); disposed.push_back(std::move(session)); session.reset();   // kept until its threads have ended (Shutdown waits)
    sessionReady = sessionStarting = false; afterReady = nullptr;
    if (post) KillTimer(post, AgentIdleTimer);
    // Unambiguous (test réel): a stop for another element / a new conversation only ever happens with no turn running —
    // a working agent keeps working (the other conversation is shown aside, its message waits in the queue).
    Diag(std::wstring(L"agent stopped (") + why + (turn.active ? L"; a turn was running: stopped with it)" : L"; no turn was running: the agent was idle)"));
    disposed.erase(std::remove_if(disposed.begin(), disposed.end(), [](const std::unique_ptr<Acp::Session>& s) { return s->WaitStopped(0); }), disposed.end());
}
// One entry of the panel's mode list -> the agent: a session mode, the option that carries the modes, or the plan option.
void SetAgentMode(const std::string& id) {
    if (!session || !sessionReady) { pendingMode = id; return; }
    const auto mv = ModesOf(agentId, agentInfo[agentId]);
    askedMode = ChatOptions::Asked{id, Ann::NowMs()};   // R46: shown until the agent confirms it
    Diag(L"mode asked: « " + W(id) + L" »");
    modeAwaited = id; modeRefused = false;   // SEC-13: no prompt until the session confirms it
    if (id == PlanOptionId) { session->SetConfigOption(mv.map.planOption, mv.map.planOn); return; }
    if (mv.current == PlanOptionId) session->SetConfigOption(mv.map.planOption, mv.map.planOff);   // leaving « Planifier » held by an option
    if (mv.viaOption) session->SetConfigOption(mv.optionId, id); else session->SetMode(id);
}
// D6: the turn's `turn.start` item gets the model and the effort the session runs with, when they were not known at its
// start (a new conversation's session starts with its first turn). Never replaces a kept value.
void FillTurnHeader() {
    if (turn.startItem >= convo.data.items.size()) return;
    auto& item = convo.data.items[turn.startItem]; const auto* ty = item.Get("type"); if (!ty || !ty->IsString() || ty->string != "turn.start") return;
    const auto& info = agentInfo[agentId];
    auto field = [&](const char* k) { const auto* v = item.Get(k); return v && v->IsString() ? v->string : std::string(); };
    std::string model = field("model"), effort = field("effort");
    const bool m = ChatOptions::CorrectTurnLabel(model, LabelOf(Acp::FindModelOption(info.options), agentId)), e = ChatOptions::CorrectTurnLabel(effort, LabelOf(Acp::FindEffortOption(info.options)));
    if (m) item.Set("model", Str(model));
    if (e) item.Set("effort", Str(effort));
    if (m || e) {
        Diag(L"turn " + W(turn.id) + L" header kept: " + W(model) + (effort.empty() ? L"" : L" · " + W(effort)));
        Send("turn.start", item);   // the same turnId again: the page updates the header it already shows (contract B §20.12)
    }
}
// What ApplyDefaults will set (R28 shows it before): the effort chosen last for this site (or just now), else Medium.
std::string WantedEffort(const Acp::ConfigOption* e) {
    if (!e) return "";
    std::string remembered = pendingEffort; if (remembered.empty()) if (auto it = projects.sites.find(site.origin); it != projects.sites.end()) remembered = it->second.effort;
    const std::string want = ChatOptions::EffortToApply(e->choices, e->currentValue, remembered);
    return want;
}
// E2 / M9: what the user chose last for this site ("" = nothing)
std::string RememberedChoice(std::string ChatStore::Project::* field) {
    if (site.origin.empty()) return "";
    const auto it = projects.sites.find(site.origin); return it == projects.sites.end() ? std::string() : it->second.*field;
}
// M: what a new session of this site gets (the choice just made, else the site's): the model when the agent still offers it
// ("" = the agent's own), the effort when offered (else Medium).
std::string NewSessionModel(const Acp::ConfigOption* m) { return m ? ChatOptions::ModelForNewSession(m->choices, ChatOptions::ChoiceForNewSession(pendingModel, RememberedChoice(&ChatStore::Project::model))) : std::string(); }
std::string NewSessionEffort(const Acp::ConfigOption* e) { return e ? ChatOptions::EffortForNewSession(e->choices, ChatOptions::ChoiceForNewSession(pendingEffort, RememberedChoice(&ChatStore::Project::effort))) : std::string(); }
// M: a mode, a model or an effort the user chose is the site's from then on — also when it was chosen before the site had
// its folder (kept in `pending…` until a session starts: ApplyDefaults) or through another provider's model (SwitchAgent).
void RememberSetting(std::string ChatStore::Project::* field, const std::string& id) {
    void RememberChoices();
    if (id.empty() || site.origin.empty() || (folder.empty() && !projects.sites.count(site.origin))) return;   // no entry without a folder: `pending…` keeps the choice
    if (projects.sites[site.origin].*field == id) return;
    projects.sites[site.origin].*field = id; RememberChoices();
}
// …and the mode: the one chosen for this site, else the agent's « ask » mode.
std::string WantedMode(const ModeView& mv) {
    auto has = [&](const std::string& id) { for (const auto& m : mv.list) if (m.id == id) return true; return false; };
    if (const std::string c = ChatOptions::ChoiceForNewSession(pendingMode, RememberedChoice(&ChatStore::Project::mode)); !c.empty() && has(c)) return c;   // M9
    for (const auto& id : mv.map.ask) if (has(id)) return id;
    for (const auto& m : mv.list) if (m.kind == "ask") return m.id;
    return "";
}
void ApplyDefaults() {   // the site's remembered choices, else « Demander » (decision 4): never a hard-coded id, the agent's own lists
    if (!session || !sessionReady) return;
    auto& info = agentInfo[agentId];
    RememberSetting(&ChatStore::Project::model, pendingModel); RememberSetting(&ChatStore::Project::effort, pendingEffort); RememberSetting(&ChatStore::Project::mode, pendingMode);   // M: chosen before the session — the site's from now on (every next element)
    auto choose = [&](const Acp::ConfigOption* o, std::string& wanted) {
        if (!o || wanted.empty()) { wanted.clear(); return; }
        if (wanted != o->currentValue) for (const auto& c : o->choices) if (c.value == wanted) { session->SetConfigOption(o->id, wanted); askedModel = ChatOptions::Asked{wanted, Ann::NowMs()}; break; }
        wanted.clear();
    };
    pendingModel = ChatOptions::ChoiceForNewSession(pendingModel, RememberedChoice(&ChatStore::Project::model));   // E2: every session, not the first one only
    choose(Acp::FindModelOption(info.options), pendingModel);
    if (const auto* e = Acp::FindEffortOption(info.options)) {   // V1.2: never the agent's « Default »; the effort chosen last for this site, else Medium
        std::string remembered = pendingEffort; if (remembered.empty()) if (auto it = projects.sites.find(site.origin); it != projects.sites.end()) remembered = it->second.effort;
        const std::string want = ChatOptions::EffortToApply(e->choices, e->currentValue, remembered);
        if (!want.empty()) { session->SetConfigOption(e->id, want); askedEffort = ChatOptions::Asked{want, Ann::NowMs()}; Diag(L"effort set to « " + W(want) + L" » (the session started with « " + W(e->currentValue) + L" »)"); }
        else Diag(L"effort « " + W(e->currentValue) + L" » kept");
    }
    pendingEffort.clear();
    // The session starts in the mode the user's own agent configuration picks (seen: Claude « auto », Codex « agent »).
    // Ours is « Demander » unless this site remembers another choice: set here, right after Started, before any prompt.
    const auto mv = ModesOf(agentId, info); std::string want = ChatOptions::ChoiceForNewSession(pendingMode, RememberedChoice(&ChatStore::Project::mode)); pendingMode.clear();   // M9
    auto has = [&](const std::string& id) { for (const auto& m : mv.list) if (m.id == id) return true; return false; };
    if (want.empty() || !has(want)) {
        want.clear();
        for (const auto& id : mv.map.ask) if (want.empty() && has(id)) want = id;                       // the agent's own « ask » mode, as mapped
        for (const auto& m : mv.list) if (want.empty() && m.kind == "ask") want = m.id;                // an agent the map does not know
    }
    if (!want.empty() && want != mv.current) { SetAgentMode(want); Diag(L"mode set to « " + W(want) + L" » (the session started in « " + W(mv.current) + L" »)"); }
    else Diag(L"mode « " + W(mv.current) + L" » kept");
    defaultsApplied = true;   // R28: from now on the composer shows the session's own values (the same, once applied)
    choicesOwed = 0;
}
// M (coordinator): « Lancer le plan » accepted the agent's plan (its own gate) — the mode the agent goes to then is the user's
// choice from now on (the site's): the next element does not start in « Planifier » again. Only a mode the chat offers.
void KeepModeAfterPlan() {
    if (!turn.planLaunched) return;
    const auto mv = ModesOf(agentId, agentInfo[agentId]);
    const std::string kept = ChatOptions::ModeAfterPlan(true, mv.current, KindOfMode(mv, mv.current)); if (kept.empty()) return;   // still in Plan
    turn.planLaunched = false;
    if (std::none_of(mv.list.begin(), mv.list.end(), [&](const ModeEntry& m) { return m.id == kept; })) return;   // a mode never offered is never kept
    RememberSetting(&ChatStore::Project::mode, kept); Diag(L"plan launched: the mode that follows, « " + W(kept) + L" », is kept as the user's choice");   // i18n-exempt: log detail
}
// M / E3: a choice made in a conversation shown aside never touched the turn that ran; that turn has ended — its session,
// if it stays, gets the choices now (what the composer has shown since), before any next message.
void GiveOwedChoices() {
    const int owed = choicesOwed; choicesOwed = 0;
    if (!owed || !session || !sessionReady) return;
    auto& info = agentInfo[agentId];
    if (owed & OwedModel) if (const auto* o = Acp::FindModelOption(info.options)) {
        const std::string want = NewSessionModel(o); if (!want.empty()) pendingModel.clear();   // "": not a model of this agent (another provider's, chosen aside) — kept as it was
        if (!want.empty() && want != o->currentValue) { session->SetConfigOption(o->id, want); askedModel = ChatOptions::Asked{want, Ann::NowMs()}; Diag(L"model set to « " + W(want) + L" » (chosen while this session's turn ran)"); } }   // i18n-exempt: log detail
    if (owed & OwedEffort) if (const auto* e = Acp::FindEffortOption(info.options)) {
        const std::string want = NewSessionEffort(e); pendingEffort.clear();
        if (!want.empty() && want != e->currentValue) { session->SetConfigOption(e->id, want); askedEffort = ChatOptions::Asked{want, Ann::NowMs()}; Diag(L"effort set to « " + W(want) + L" » (chosen while this session's turn ran)"); } }   // i18n-exempt: log detail
    if (owed & OwedMode) {
        const auto mv = ModesOf(agentId, info); const std::string want = WantedMode(mv); pendingMode.clear();
        if (!want.empty() && want != mv.current) { SetAgentMode(want); Diag(L"mode set to « " + W(want) + L" » (chosen while this session's turn ran)"); } }   // i18n-exempt: log detail
}
void OnStarted(const Acp::StartResult& r) {
    sessionStarting = false; auto& info = agentInfo[agentId]; started = r;
    Diag(std::wstring(L"session start: ") + (r.state == Acp::StartState::Ready ? (r.resumed ? L"ready (resumed)" : L"ready (new)") : W(Acp::ErrorCodeName(r.error.code))) + (r.error.detail.empty() ? L"" : L" — " + W(ClipUtf8(r.error.detail, 300))));
    switch (r.state) {
    case Acp::StartState::Ready: {
        sessionReady = true; info.status = "ready"; info.detail = TR(chat_agent_installed); info.progress = -1; info.auth.clear(); info.cli = info.prepared = true;
        info.options = r.configOptions; info.modes = r.modes; info.caps = r.capabilities; info.commands = r.commands;
        if (const auto* mo = Acp::FindModelOption(info.options); mo && !agentFirstModel.count(agentId)) agentFirstModel[agentId] = mo->currentValue;
        { void SaveOptionsCache(); SaveOptionsCache(); }   // R13: shown from the next launch's first opening
        if (!r.resumed || convo.data.head.promptVersion != ChatPrompt::Version) convo.preambleSent = false;
        convo.data.head.sessionId = r.sessionId; convo.data.head.agentId = agentId;
        ClearStrip("auth"); ApplyDefaults(); SendState(); SendCommands();
        if (stats.sessionAt == 0) stats.sessionAt = Ann::NowMs();
        ArmAgentIdle();
        if (auto go = std::move(afterReady)) { afterReady = nullptr; go(); }
        return; }
    case Acp::StartState::NeedsPrepare:
        session.reset();
        PrepareAgent(agentId, [](bool ok) { if (ok) StartSession(); else if (turn.active) { Acp::Error e; e.code = Acp::ErrorCode::NeedsPrepare; turn.failure = e; afterReady = nullptr; FinishTurn(turn.cancelled ? "cancelled" : "error"); } });
        return;
    case Acp::StartState::AuthRequired:
        info.status = "notConnected"; info.detail = TR(chat_agent_not_connected); info.auth = r.authMethods;
        Strip("auth", "danger", TRF(chat_strip_disconnected, W(Agent().name)), "connect", TR(chat_action_connect));
        break;
    case Acp::StartState::NotInstalled: info.status = "notInstalled"; info.detail = TR(chat_agent_not_installed); break;
    default: info.status = "error"; info.detail = r.error.code == Acp::ErrorCode::CliTooOld ? TR(chat_agent_too_old) : TR(chat_agent_unavailable); break;
    }
    // not ready: no process is left. The object is kept after AuthRequired only (Session::Authenticate starts from it).
    if (session && r.state != Acp::StartState::AuthRequired) { session->Dispose(); disposed.push_back(std::move(session)); session.reset(); }
    afterReady = nullptr; SendState();
    if (turn.active) { turn.failure = r.error; FinishTurn("error"); }
    else if (r.state != Acp::StartState::AuthRequired) SendError(ProblemFor(r.error, agentId), r.error.detail);
}
void StartSession() {
    if (session && !sessionReady && !sessionStarting) { session->Dispose(); disposed.push_back(std::move(session)); session.reset(); }   // an object left by AuthRequired
    if (session || sessionStarting || !Usable()) return;
    const auto* def = AgentOf(agentId); if (!def) return;
    session = std::make_unique<Acp::Session>();
    const HWND target = post; session->SetWake([target] { if (target) PostMessageW(target, AcpWake, 0, 0); });
    Acp::StartOptions o; o.resolve = AgentRuntime::MakeResolver(RtEnv(), def->rt); o.cwd = U8(folder);
    o.resumeSessionId = convo.data.head.agentId == agentId ? convo.data.head.sessionId : std::string();
    o.clientName = "browser";
#ifdef BROWSER_VERSION_TEXT
    o.clientVersion = BROWSER_VERSION_TEXT;
#endif
    {   // our MCP bridge over the annotation store, restricted to this site: the agent can list / claim / resolve annotations
        const auto target2 = Ann::ConnectTarget();
        if (GetFileAttributesW(target2.bridgeExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            Acp::McpServer m; m.name = Ann::ServerName(); m.command = U8(target2.bridgeExe); m.args = {"--store", U8(target2.storeRoot), "--site", site.origin}; o.mcpServers.push_back(std::move(m));
        }
    }
    sessionStarting = true; sessionReady = false;
    if (!session->Start(std::move(o))) { sessionStarting = false; session.reset(); Diag(L"session start refused"); return; }
    SetAgentStatus(agentId, "starting", TR(chat_agent_starting));
    Diag(L"session starting: " + W(agentId) + L" in " + folder);
}
void EnsureSession(std::function<void()> ready) {
    if (sessionReady && session) { ready(); return; }
    afterReady = std::move(ready);
    if (!sessionStarting && !preparing) StartSession();   // M13: an object left by « sign-in needed » counts as none (StartSession disposes it)
    if (!session && !sessionStarting && !preparing && turn.active) {   // M13: nothing could start — the turn ends in error instead of waiting forever
        afterReady = nullptr; Acp::Error e; e.code = Acp::ErrorCode::NotReady; turn.failure = e; Diag(L"no session could start: the turn ends in error"); FinishTurn("error");   // i18n-exempt: log detail
    }
}
void ArmAgentIdle() { if (post && !turn.active) SetTimer(post, AgentIdleTimer, Open() || BubbleOpen() || (CommentLayout() && Ann::s.active) ? AgentIdleMs() : PanelIdleMs(), nullptr); }

// ---------------------------------------------------------------- diffs
SJ::Value DiffJson(const std::string& path, const CS::FileDiff& d) {
    SJ::Value o = SJ::Value::Obj(); o.Set("path", Str(path)); o.Set("added", SJ::Value::Int(d.additions)); o.Set("removed", SJ::Value::Int(d.deletions));
    SJ::Value lines = SJ::Value::Arr(); std::size_t n = 0; bool truncated = d.truncated; std::size_t at = 0; const std::string& u = d.unified;
    while (at < u.size()) {
        std::size_t end = u.find('\n', at); if (end == std::string::npos) end = u.size();
        const std::string line = u.substr(at, end - at); at = end + 1;
        if (line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0 || line.rfind("\\ ", 0) == 0) continue;
        if (n >= 400) { truncated = true; break; }
        SJ::Value l = SJ::Value::Obj(); const char op = line.empty() ? ' ' : line[0];
        l.Set("op", Str(std::string(1, op == '+' || op == '-' || op == '@' ? op : ' '))); l.Set("text", Str(ClipUtf8(op == '@' ? line : (line.empty() ? line : line.substr(1)), 2000)));
        lines.Push(std::move(l)); ++n;
    }
    o.Set("lines", std::move(lines)); if (truncated) o.Set("truncated", SJ::Value::Bool(true));
    return o;
}
std::string RelPath(const std::string& path) {   // a path of the agent, shown relative to the project folder
    std::string p = path; for (auto& c : p) if (c == '\\') c = '/';
    std::string root = U8(folder); for (auto& c : root) if (c == '\\') c = '/';
    if (!root.empty() && p.size() > root.size() + 1 && ChatPrompt::detail::StartsWithFolded(p, root + "/")) return p.substr(root.size() + 1);
    return p;
}
std::string FileName(const std::string& path) { const auto s = path.find_last_of("/\\"); return s == std::string::npos ? path : path.substr(s + 1); }
SJ::Value AgentDiff(const Acp::Diff& d) {
    const auto fd = CS::DiffContents(d.path, d.hasOldText ? &d.oldText : nullptr, &d.newText, true, 96 * 1024);
    SJ::Value o = DiffJson(RelPath(d.path), fd); if (d.truncated) o.Set("truncated", SJ::Value::Bool(true)); return o;
}

// ---------------------------------------------------------------- agent events -> panel
void CloseThought() {
    if (turn.thoughtId.empty()) return;
    const int ms = int(Ann::NowMs() - turn.thoughtStart);
    SJ::Value m = SJ::Value::Obj(); m.Set("turnId", Str(turn.id)); m.Set("id", Str(turn.thoughtId)); m.Set("text", Str(std::string())); m.Set("done", SJ::Value::Bool(true)); m.Set("durationMs", SJ::Value::Int(ms));
    Send("thought", std::move(m));
    if (auto it = turn.itemOf.find(turn.thoughtId); it != turn.itemOf.end()) { auto& item = convo.data.items[it->second]; item.Set("done", SJ::Value::Bool(true)); item.Set("durationMs", SJ::Value::Int(ms)); }
    turn.thoughtId.clear();
}
// PERF-20: appended in place (one copy of the chunk, not of the whole message), and capped: what is kept with the
// conversation stops growing at MaxKeptText (the panel still gets every chunk as it comes).
constexpr std::size_t MaxKeptText = 1u << 20;
void AppendText(const std::string& id, const std::string& more) {
    auto it = turn.itemOf.find(id); if (it == turn.itemOf.end()) return;
    auto& item = convo.data.items[it->second];
    SJ::Value* t = nullptr; for (auto& f : item.members) if (f.first == "text") t = &f.second;
    if (!t || !t->IsString()) t = &item.Set("text", Str(std::string()));
    if (t->string.size() >= MaxKeptText) return;
    t->string += more;
    if (t->string.size() > MaxKeptText) t->string = ClipUtf8(t->string, MaxKeptText);
}
SJ::Value ToolJson(const Acp::ToolCall& t, bool update) {
    SJ::Value m = SJ::Value::Obj(); m.Set("id", Str(t.id));
    if (!update || t.hasTitle) m.Set("title", Str(ClipUtf8(t.title.empty() ? t.kind : t.title, 400)));
    if (!update) m.Set("kind", Str(t.kind.empty() || t.kind == "switch_mode" ? std::string("other") : t.kind));
    if (!update || t.hasStatus) m.Set("status", Str(t.status.empty() ? std::string("pending") : t.status));
    if (t.hasLocations || (!update && !t.locations.empty())) { SJ::Value ls = SJ::Value::Arr(); for (const auto& l : t.locations) { if (ls.items.size() >= 20) break; SJ::Value o = SJ::Value::Obj(); o.Set("path", Str(RelPath(l.path))); if (l.line >= 0) o.Set("line", SJ::Value::Int(l.line)); ls.Push(std::move(o)); } m.Set("locations", std::move(ls)); }
    if (!t.diffs.empty()) {   // SEC-14: kept with the conversation: a file that may hold secrets (.env, *.pem, .npmrc…) keeps its name and counts, never its lines
        SJ::Value d = AgentDiff(t.diffs.front());
        if (CS::MayHoldSecrets(FileName(t.diffs.front().path))) d.Set("lines", SJ::Value::Arr());
        m.Set("diff", std::move(d));
    }
    return m;
}
// ---- tools in words (better-writing: verb first, one whole sentence per case, the product's own nouns)
// « … » of a step or a card: the annotation's note, short. Empty when this session never saw it.
std::wstring NoteOf(const std::string& annotation) {
    if (annotation.empty()) return L"";
    for (const auto& e : turn.elements) if (e.annId == annotation) return W(ChatPrompt::OneLine(turn.text, 40));
    if (auto it = annotationNotes.find(annotation); it != annotationNotes.end()) return W(ChatPrompt::OneLine(it->second, 40));
    if (auto it = Ann::historyEntries.find(annotation); it != Ann::historyEntries.end()) return W(ChatPrompt::OneLine(it->second.annotation.note, 40));
    return L"";
}
// What one of our annotation tools does (asked: the infinitive of the card and of a running step) or did (the step once done).
std::wstring OurToolTitle(ChatTools::Ours tool, const std::string& annotation, bool done, int count = -1) {
    const std::wstring note = NoteOf(annotation); const bool named = !note.empty();
    switch (tool) {
    case ChatTools::Ours::List: return !done ? std::wstring(TR(chat_tool_list)) : count < 0 ? std::wstring(TR(chat_tool_list_done)) : count == 0 ? std::wstring(TR(chat_tool_list_done_none)) : TRP(chat_tool_list_done, count);
    case ChatTools::Ours::Get: return done ? (named ? TRF(chat_tool_get_done, note) : std::wstring(TR(chat_tool_get_done_any))) : (named ? TRF(chat_tool_get, note) : std::wstring(TR(chat_tool_get_any)));
    case ChatTools::Ours::Claim: return done ? (named ? TRF(chat_tool_claim_done, note) : std::wstring(TR(chat_tool_claim_done_any))) : (named ? TRF(chat_tool_claim, note) : std::wstring(TR(chat_tool_claim_any)));
    case ChatTools::Ours::Resolve: return done ? (named ? TRF(chat_tool_resolve_done, note) : std::wstring(TR(chat_tool_resolve_done_any))) : (named ? TRF(chat_tool_resolve, note) : std::wstring(TR(chat_tool_resolve_any)));
    case ChatTools::Ours::Release: return done ? (named ? TRF(chat_tool_release_done, note) : std::wstring(TR(chat_tool_release_done_any))) : (named ? TRF(chat_tool_release, note) : std::wstring(TR(chat_tool_release_any)));
    default: return L"";
    }
}
// How a finished step of ours is said inside the folded summary of a group of steps (contract B 1.3, « summary »):
// the verb in the past without its subject, then what it was done to. The panel merges equal verbs (« a lu 3
// annotations ») and lists the rest; a group of one step shows the step's own title (OurToolTitle) as it is.
SJ::Value OurToolSummary(ChatTools::Ours tool, const std::string& annotation, int count) {
    SJ::Value s = SJ::Value::Obj(); const std::wstring note = NoteOf(annotation);
    const auto counted = [&](int n) { s.Set("object", Str(TRP(chat_sum_count, n))); s.Set("count", SJ::Value::Int(n)); s.Set("one", Str(std::wstring(TR(chat_sum_unit_single)))); s.Set("many", Str(std::wstring(TR(chat_sum_unit_plural)))); };
    const auto about = [&](const wchar_t* verb) {   // one annotation: by its note when we know it, else counted
        s.Set("verb", Str(std::wstring(verb)));
        if (!note.empty()) s.Set("object", Str(TRF(chat_sum_named, note))); else counted(1);
    };
    switch (tool) {
    case ChatTools::Ours::List:
        if (count == 0) { s.Set("verb", Str(std::wstring(TR(chat_sum_found)))); s.Set("object", Str(std::wstring(TR(chat_sum_none)))); }
        else { s.Set("verb", Str(std::wstring(TR(chat_sum_read)))); if (count > 0) counted(count); else s.Set("object", Str(std::wstring(TR(chat_sum_pending)))); }
        break;
    case ChatTools::Ours::Get: about(TR(chat_sum_opened)); break;
    case ChatTools::Ours::Claim: about(TR(chat_sum_claimed)); break;
    case ChatTools::Ours::Resolve: about(TR(chat_sum_marked)); s.Set("after", Str(std::wstring(TR(chat_sum_done)))); s.Set("afterMany", Str(std::wstring(TR(chat_sum_done_many)))); break;
    case ChatTools::Ours::Release: about(TR(chat_sum_released)); break;
    default: break;
    }
    return s;
}
// The files an agent's tool call writes (ACP: kind edit / delete / move, its locations and diffs; the raw input's file
// path when it gives none). Only these count as the turn's changes: a file changed meanwhile by another program or
// another agent working in the same folder is never counted, nor undone.
void NoteAgentFiles(const Acp::ToolCall& t) {
    if (t.hasKind && !t.kind.empty()) turn.toolKinds[t.id] = t.kind;
    const auto k = turn.toolKinds.find(t.id); const std::string kind = k == turn.toolKinds.end() ? std::string() : k->second;
    const bool writes = kind == "edit" || kind == "delete" || kind == "move";
    std::vector<std::string> named;
    for (const auto& d : t.diffs) named.push_back(d.path);
    if (writes) {
        for (const auto& l : t.locations) named.push_back(l.path);
        SJ::Value v;
        if (!t.rawInput.empty() && SJ::Parse(t.rawInput, v) && v.IsObject())
            for (const char* key : {"file_path", "filePath", "path", "notebook_path", "new_path", "old_path"}) if (const auto* f = v.Get(key); f && f->IsString()) named.push_back(f->string);   // i18n-exempt: tool argument names
    }
    const std::string root = U8(folder);
    for (const auto& f : named) {
        const std::string rel = ChatPrompt::ProjectRelative(f, root);
        if (!rel.empty() && std::find(turn.agentFiles.begin(), turn.agentFiles.end(), rel) == turn.agentFiles.end() && turn.agentFiles.size() < 5000) turn.agentFiles.push_back(rel);
    }
}
ChatTools::Ours OurToolOf(const Acp::ToolCall& t) { return ChatTools::OurTool(ChatTools::ParseMcpName(t.title), Ann::ServerName()); }
// Notes seen in a tool's result (the list names every annotation it returns): remembered for the next step lines.
void LearnNotes(const std::string& resultText) {
    if (resultText.empty()) return;
    for (auto& a : ChatTools::AnnotationsIn(resultText)) { if (annotationNotes.size() > 2000) annotationNotes.clear(); if (!a.second.empty()) annotationNotes[a.first] = ClipUtf8(a.second, 400); }
}
std::wstring PermissionTitle(const Acp::ToolCall& t) {
    const std::wstring who = AgentShort(agentId); std::string file;
    const auto mcp = ChatTools::ParseMcpName(t.title);
    if (const auto ours = ChatTools::OurTool(mcp, Ann::ServerName()); ours != ChatTools::Ours::None) return OurToolTitle(ours, ChatTools::AnnotationIdOf(t.rawInput), false);
    if (mcp.mcp) return TRF(chat_perm_mcp, who, W(mcp.tool), W(mcp.server));   // another MCP server: its tool and its name, as they are
    if (!t.diffs.empty()) file = FileName(t.diffs.front().path); else if (!t.locations.empty()) file = FileName(t.locations.front().path);
    if (t.kind == "edit" && !file.empty()) return TRF(chat_perm_edit, who, W(file));
    if (t.kind == "delete" && !file.empty()) return TRF(chat_perm_delete, who, W(file));
    if (t.kind == "read" && !file.empty()) return TRF(chat_perm_read, who, W(file));
    if (t.kind == "execute") return TRF(chat_perm_run, who);
    if (t.kind == "fetch") return TRF(chat_perm_fetch, who);
    if (t.kind == "switch_mode") return TRF(chat_perm_mode, who);
    return t.title.empty() ? TRF(chat_perm_generic, who) : W(ClipUtf8(t.title, 200));
}
std::string CommandOf(const Acp::ToolCall& t) {
    SJ::Value v; if (t.rawInput.empty() || !SJ::Parse(t.rawInput, v) || !v.IsObject()) return "";
    for (const char* k : {"command", "cmd"}) if (const auto* c = v.Get(k); c && c->IsString()) return ClipUtf8(c->string, 2000);
    return "";
}
std::string CurrentModeKind() { const auto mv = ModesOf(agentId, agentInfo[agentId]); return KindOfMode(mv, mv.current); }
void SendPlan(const std::vector<Acp::PlanEntry>& entries, bool proposed) {
    SJ::Value m = SJ::Value::Obj(); m.Set("turnId", Str(turn.id)); if (turn.planId.empty()) turn.planId = "plan-" + turn.id; m.Set("id", Str(turn.planId));
    SJ::Value arr = SJ::Value::Arr();
    for (const auto& e : entries) { if (arr.items.size() >= 60) break; SJ::Value o = SJ::Value::Obj(); o.Set("content", Str(ClipUtf8(e.content, 600))); o.Set("status", Str(e.status == "in_progress" || e.status == "completed" ? e.status : std::string("pending"))); arr.Push(std::move(o)); }
    m.Set("entries", std::move(arr)); if (proposed) m.Set("proposed", SJ::Value::Bool(true));
    if (auto it = turn.itemOf.find(turn.planId); it != turn.itemOf.end()) convo.data.items[it->second] = Item("plan", m); else turn.itemOf[turn.planId] = Keep("plan", m);
    Send("plan", std::move(m));
}
// A plan written as text (the tool call that asks to leave Plan mode carries it): one entry per list line.
std::vector<Acp::PlanEntry> PlanFromText(const std::string& text) {
    std::vector<Acp::PlanEntry> out; std::size_t at = 0;
    while (at < text.size() && out.size() < 40) {
        std::size_t end = text.find('\n', at); if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at); at = end + 1;
        std::size_t i = 0; while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        std::size_t j = i; while (j < line.size() && line[j] >= '0' && line[j] <= '9') ++j;
        const bool numbered = j > i && j < line.size() && (line[j] == '.' || line[j] == ')'); const bool bullet = i < line.size() && (line[i] == '-' || line[i] == '*') && i + 1 < line.size() && line[i + 1] == ' ';
        if (!numbered && !bullet) continue;
        line = line.substr(numbered ? j + 1 : i + 2); while (!line.empty() && line[0] == ' ') line.erase(0, 1);
        if (!line.empty()) { Acp::PlanEntry e; e.content = line; e.status = "pending"; out.push_back(std::move(e)); }
    }
    return out;
}
// The step line of one of our annotation tools: what it does while it runs, what it did once done (« A lu 2 annotations »).
void WordOurCall(Turn::OurCall& call, const Acp::ToolCall& t, SJ::Value& message) {
    if (!t.rawInput.empty() && t.rawInput != "{}") call.input = t.rawInput;
    if (t.hasContent || !t.text.empty()) { LearnNotes(t.text); if (call.tool == ChatTools::Ours::List) { const int n = ChatTools::ListCount(t.text); if (n >= 0) call.count = n; } }
    if (t.hasStatus || !t.status.empty()) call.done = t.status == "completed";
    message.Set("title", Str(OurToolTitle(call.tool, call.annotation, call.done, call.count)));
    // « other », never « read »: the panel folds finished steps into a count by kind, and « read » is counted as files
    // (« A lu 1 fichier »). An annotation is not a file: the folded line says « 1 étape », the step itself says the rest.
    message.Set("kind", Str(std::string("other")));
    if (call.done) message.Set("summary", OurToolSummary(call.tool, call.annotation, call.count));
    // a batch turn: the call that just succeeded moves its annotation's line of the card (once)
    if (call.done && !call.reported) { call.reported = true; BatchToolDone(call.tool, call.annotation, call.input); }
}
void OnPermission(const Acp::PermissionRequest& p) {
    if (!turn.active) { if (session) session->RespondPermission(p.requestId, ""); return; }
    CloseThought(); turn.lastMessageId.clear();
    // Plan mode: the agent asks to leave it with its plan -> the card « Plan proposé » (Lancer le plan / Modifier).
    if (!p.question && p.toolCall.kind == "switch_mode" && CurrentModeKind() == "plan") {
        auto entries = turn.lastPlan.empty() ? PlanFromText(p.toolCall.text) : turn.lastPlan;
        if (!entries.empty()) { turn.planPermission = p.requestId; turn.planOptions = p.options; SendPlan(entries, true); return; }
    }
    // Our own annotation tools only read and write the annotation store (never the code, never a command): allowed once,
    // without a card, in every mode — the step line in the thread says what was done (design/chat-integration.md).
    // Never « always »: each call is answered here, so nothing is left configured in the agent.
    const auto mcp = p.question ? ChatTools::McpName{} : ChatTools::ParseMcpName(p.toolCall.title);
    if (const auto ours = ChatTools::OurTool(mcp, Ann::ServerName()); ours != ChatTools::Ours::None && session && ChatTools::LooksLikeOurs(ours, p.toolCall.kind, p.toolCall.rawInput)) {   // SEC-28: not on the title alone
        for (const auto& o : p.options) if (o.kind == "allow_once" && ChatStore::ValidId(o.id)) {
            session->RespondPermission(p.requestId, o.id);
            if (!p.toolCall.id.empty() && !turn.ourCalls.count(p.toolCall.id)) { auto& call = turn.ourCalls[p.toolCall.id]; call.tool = ours; call.annotation = ChatTools::AnnotationIdOf(p.toolCall.rawInput); call.input = p.toolCall.rawInput; }
            Diag(L"annotation tool allowed without a card: " + W(mcp.tool));
            return;
        }
    }
    SJ::Value m = SJ::Value::Obj(); m.Set("requestId", Str(p.requestId)); if (!p.toolCall.id.empty()) m.Set("toolCallId", Str(p.toolCall.id));
    m.Set("title", p.question ? Str(ClipUtf8(p.toolCall.title, 300)) : Str(PermissionTitle(p.toolCall)));
    if (mcp.mcp) {   // « Détails » of the card (contract B 1.2): the raw name, and what we know of it
        SJ::Value tool = SJ::Value::Obj(); tool.Set("name", Str(ClipUtf8(p.toolCall.title, 300))); tool.Set("server", Str(mcp.server));
        if (const auto ours = ChatTools::OurTool(mcp, Ann::ServerName()); ours != ChatTools::Ours::None) { tool.Set("label", Str(OurToolTitle(ours, ChatTools::AnnotationIdOf(p.toolCall.rawInput), false))); tool.Set("serverLabel", Str(T8(I18n::Id::chat_tool_server_label))); }
        m.Set("tool", std::move(tool));
    }
    if (!p.question && !p.toolCall.rawInput.empty() && p.toolCall.rawInput != "{}") m.Set("input", Str(ClipUtf8(p.toolCall.rawInput, 4000)));
    m.Set("kind", Str(p.toolCall.kind.empty() || p.toolCall.kind == "switch_mode" ? std::string("other") : p.toolCall.kind));
    if (!p.toolCall.diffs.empty()) m.Set("diff", AgentDiff(p.toolCall.diffs.front()));
    if (const std::string cmd = CommandOf(p.toolCall); !cmd.empty()) m.Set("command", Str(cmd));
    SJ::Value opts = SJ::Value::Arr(); bool first = true;
    const auto neverModes = ModesOf(agentId, agentInfo[agentId]).map.never;   // SEC-02 (review): a mode that asks for nothing is never offered on a card
    for (const auto& o : p.options) {
        if (!ChatStore::ValidId(o.id) || In(neverModes, o.id)) continue; SJ::Value v = SJ::Value::Obj(); v.Set("optionId", Str(o.id)); v.Set("name", Str(ClipUtf8(o.name.empty() ? o.id : o.name, 200)));
        std::string kind = o.kind;   // a question's choices: the first (« skip ») refuses, the others answer
        if (kind != "allow_once" && kind != "allow_always" && kind != "reject_once" && kind != "reject_always") kind = p.question && first ? "reject_once" : "allow_once";
        v.Set("kind", Str(kind)); opts.Push(std::move(v)); first = false;
    }
    m.Set("options", std::move(opts));
    waitingPermissions[p.requestId] = m;   // shown again if the panel's WebView is created (or re-created) while it waits
    Send("permission.request", std::move(m));
    Diag(L"permission asked: " + W(p.toolCall.kind) + (mcp.mcp ? L" " + W(ClipUtf8(p.toolCall.title, 120)) : std::wstring()));   // SEC-14: an MCP tool's name, never a command line or a file's text
    RevealCardSoon();   // R47: its card and its buttons on screen (the site's page scrolls softly if needed)
}
void OnEvent(const Acp::Event& e) {
    using T = Acp::Event::Type; auto& info = agentInfo[agentId];
    switch (e.type) {
    case T::Started: OnStarted(e.started); break;
    case T::MessageChunk: {
        if (!turn.active || e.text.empty()) break;
        CloseThought();
        if (turn.lastMessageId.empty()) { turn.lastMessageId = "m" + turn.id + "-" + std::to_string(++turn.messageSeq); SJ::Value item = SJ::Value::Obj(); item.Set("turnId", Str(turn.id)); item.Set("messageId", Str(turn.lastMessageId)); item.Set("text", Str(std::string())); turn.itemOf[turn.lastMessageId] = Keep("message.agentChunk", item); turn.summaryText.clear(); }
        AppendText(turn.lastMessageId, e.text); turn.summaryText += e.text;
        FillTurnHeader();   // D6: the session is ready by now: its model and effort go with the turn
        if (stats.firstTextAt == 0 && stats.sendAt > 0) { stats.firstTextAt = Ann::NowMs(); Diag(L"first agent text " + Ann::Ms(stats.firstTextAt - stats.sendAt) + L" ms after the send"); }
        SJ::Value m = SJ::Value::Obj(); m.Set("turnId", Str(turn.id)); m.Set("messageId", Str(turn.lastMessageId)); m.Set("text", Str(e.text)); Send("message.agentChunk", std::move(m));
        break; }
    case T::ThoughtChunk: {
        if (!turn.active || e.text.empty()) break;
        turn.lastMessageId.clear();
        if (turn.thoughtId.empty()) { turn.thoughtId = "th" + turn.id + "-" + std::to_string(++turn.thoughtSeq); turn.thoughtStart = Ann::NowMs(); SJ::Value item = SJ::Value::Obj(); item.Set("turnId", Str(turn.id)); item.Set("id", Str(turn.thoughtId)); item.Set("text", Str(std::string())); item.Set("done", SJ::Value::Bool(true)); turn.itemOf[turn.thoughtId] = Keep("thought", item); }
        AppendText(turn.thoughtId, e.text);
        SJ::Value m = SJ::Value::Obj(); m.Set("turnId", Str(turn.id)); m.Set("id", Str(turn.thoughtId)); m.Set("text", Str(e.text)); Send("thought", std::move(m));
        break; }
    case T::ToolCall: {
        if (!turn.active || !ChatStore::ValidId(e.tool.id)) break;
        CloseThought(); turn.lastMessageId.clear(); NoteAgentFiles(e.tool);
        SJ::Value m = ToolJson(e.tool, false); m.Set("turnId", Str(turn.id));
        if (const auto ours = OurToolOf(e.tool); ours != ChatTools::Ours::None) {   // one of our annotation tools: the step says it in words
            auto& call = turn.ourCalls[e.tool.id]; call.tool = ours; if (call.annotation.empty()) call.annotation = ChatTools::AnnotationIdOf(e.tool.rawInput);
            WordOurCall(call, e.tool, m);
        }
        if (auto it = turn.itemOf.find("tool:" + e.tool.id); it != turn.itemOf.end()) convo.data.items[it->second] = Item("toolCall", m); else turn.itemOf["tool:" + e.tool.id] = Keep("toolCall", m);
        Send("toolCall", std::move(m));
        break; }
    case T::ToolCallUpdate: {
        if (!turn.active || !ChatStore::ValidId(e.tool.id)) break;
        NoteAgentFiles(e.tool);
        SJ::Value m = ToolJson(e.tool, true);
        if (auto call = turn.ourCalls.find(e.tool.id); call != turn.ourCalls.end() && call->second.tool != ChatTools::Ours::None) {
            if (call->second.annotation.empty()) call->second.annotation = ChatTools::AnnotationIdOf(e.tool.rawInput);
            WordOurCall(call->second, e.tool, m);
        }
        if (auto it = turn.itemOf.find("tool:" + e.tool.id); it != turn.itemOf.end()) { auto& item = convo.data.items[it->second]; for (const auto& f : m.members) if (f.first != "diff") item.Set(f.first, f.second); }
        else { SJ::Value full = ToolJson(e.tool, false); full.Set("turnId", Str(turn.id)); turn.itemOf["tool:" + e.tool.id] = Keep("toolCall", full); Send("toolCall", std::move(full)); break; }
        Send("toolCallUpdate", std::move(m));
        break; }
    case T::Plan: if (turn.active) { turn.lastPlan = e.plan; SendPlan(e.plan, false); } break;
    case T::PermissionRequest: OnPermission(e.permission); break;
    case T::PermissionDismissed: {
        if (e.permission.requestId == turn.planPermission) { turn.planPermission.clear(); break; }
        waitingPermissions.erase(e.permission.requestId);
        SJ::Value m = SJ::Value::Obj(); m.Set("requestId", Str(e.permission.requestId)); Send("permission.cancel", std::move(m)); break; }
    case T::ModesChanged: info.modes = e.modes; KeepModeAfterPlan(); SendState(); ModeSettled(); break;
    case T::ConfigOptionsChanged: { info.options = e.configOptions; KeepModeAfterPlan(); SendState(); void SaveOptionsCache(); SaveOptionsCache(); ModeSettled(); break; }
    case T::CommandsChanged: info.commands = e.commands; SendCommands(); break;
    case T::SessionTitle: if (!e.text.empty()) { convo.data.head.title = ChatPrompt::OneLine(e.text, 60); SendState(); } break;
    case T::Usage: {   // context gauge (contract B 1.2): relayed as it comes (also after a compaction), kept with the conversation
        auto& h = convo.data.head; const std::int64_t used = std::max<std::int64_t>(0, e.usage.used), size = std::max<std::int64_t>(0, e.usage.size);
        if (used != h.usedTokens || size != h.windowTokens) Diag(L"usage " + std::to_wstring(used) + L" / " + std::to_wstring(size));
        h.usedTokens = used; h.windowTokens = size;
        SJ::Value m = SJ::Value::Obj(); m.Set("used", SJ::Value::Int(h.usedTokens)); m.Set("size", SJ::Value::Int(h.windowTokens)); Send("usage", std::move(m));
        break; }
    case T::AuthStatus:   // Claude's adapter answers « ready » even when signed out: its own status is what tells
        Diag(L"agent login: " + W(ClipUtf8(e.text, 40)));
        if (e.text == "none") { info.status = "notConnected"; info.detail = TR(chat_agent_not_connected); if (info.auth.empty()) info.auth = started.authMethods;
            Strip("auth", "danger", TRF(chat_strip_disconnected, W(Agent().name)), "connect", TR(chat_action_connect)); SendState(); }
        else if (info.status == "notConnected") { info.status = "ready"; info.detail = TR(chat_agent_installed); ClearStrip("auth"); SendState(); }
        break;
    case T::Notice: Diag(L"agent notice (" + W(e.severity) + L"): " + W(ClipUtf8(e.text, 300))); break;
    case T::TurnEnded: if (turn.active) { turn.failure = e.error; FinishTurn(e.error.Ok() ? (e.stopReason.empty() ? std::string("end_turn") : e.stopReason) : std::string("error")); } break;
    case T::RequestFailed: Diag(L"agent refused " + W(e.request) + L": " + W(Acp::ErrorCodeName(e.error.code)) + L" " + W(ClipUtf8(e.error.detail, 200))); ModeRefused(e); SendState(); break;
    case T::Warning: Diag(L"agent line ignored: " + W(ClipUtf8(e.text, 200))); break;
    case T::Exited:
        Diag(L"agent exited (" + std::to_wstring(e.exitCode) + L"): " + W(ClipUtf8(e.error.detail, 300)));
        sessionReady = sessionStarting = false; modeAwaited.clear(); modeRefused = false; if (session) { disposed.push_back(std::move(session)); session.reset(); }
        if (turn.active) { turn.failure = e.error; FinishTurn("error"); } else { info.status = "ready"; info.detail = TR(chat_agent_installed); SendState(); }
        break;
    }
}
void PumpEvents() {
    if (!session) return;
    std::vector<Acp::Event> events; session->Poll(events);
    for (const auto& e : events) { if (!session && e.type != Acp::Event::Type::Started) break; OnEvent(e); }
}

// ---------------------------------------------------------------- annotations (decision 6: one story)
// Each element sent becomes an annotation of the store, claimed at once by the chat's agent; resolved with the agent's
// last words at the end of the turn, released with the reason when the turn stops or fails.
void CloseAnnotations(const std::vector<Element>& elements, bool resolved, const std::string& summary);
void WriteAnnotations(std::vector<Element>& elements, const std::string& note) {
    const std::string actor = Agent().actor, origin = site.origin; const std::wstring pid = profileId;
    std::wstring pname = DefaultProfileLook().name; if (const auto* p = CurrentProfile()) pname = p->name;
    for (auto& e : elements) {
        BA::Annotation a = e.pending; a.note = ClipUtf8(note, 3900); a.id = BA::NewAnnotationId(); a.site = origin;
        a.source.profileId = U8(pid); a.source.profileName = U8(pname); BA::Sanitize(a);
        auto png = e.png.empty() ? nullptr : std::make_shared<std::string>(e.png);
        if (png) { a.screenshot.scale = e.shotScale; a.screenshot.crop = e.shotCrop; }
        e.annId = a.id;
        struct Out { BA::Outcome write, claim; BA::ClaimResult lease; std::string siteKey; };
        auto out = std::make_shared<Out>(); const std::string elementId = e.id, turnId = turn.id;
        Ann::Run([a, png, out, actor]() mutable {
                std::lock_guard<std::mutex> g(Ann::writeLock);
                out->write = Ann::Store().WriteAnnotation(a, png.get(), false); out->siteKey = a.siteKey;
                if (out->write.ok()) out->claim = Ann::Store().Claim(a.siteKey, a.id, actor, "", out->lease);
            },
            [out, elementId, turnId, id = a.id] {
                Diag(L"annotation " + W(id) + L": write " + W(BA::CodeName(out->write.code)) + L", claim " + W(BA::CodeName(out->claim.code)));
                if (!out->write.ok()) return;
                Ann::sessionIds.insert(id); Ann::seenWorking.insert(id); Ann::seenDone.insert(id);   // the chat shows its own progress: no launcher card for it
                auto settle = [&](Element& el) { el.annSite = out->siteKey; el.token = out->claim.ok() ? out->lease.token : std::string(); };
                if (turn.active && turn.id == turnId) { for (auto& el : turn.elements) if (el.id == elementId) settle(el); }
                else for (auto it = orphanAnnotations.begin(); it != orphanAnnotations.end(); ++it) if (it->annId == id) {   // its turn is over: close it now
                    settle(*it); const Element late = *it; orphanAnnotations.erase(it);
                    if (!late.token.empty()) CloseAnnotations({late}, late.closeResolved, late.closeSummary);
                    break; }
                Ann::RefreshStatus();
            });
    }
}
void CloseAnnotations(const std::vector<Element>& elements, bool resolved, const std::string& summary) {
    const std::string actor = Agent().actor;
    for (const auto& e : elements) {
        if (e.annId.empty()) continue;
        if (e.annSite.empty() || e.token.empty()) { Element late = e; late.closeResolved = resolved; late.closeSummary = summary; late.png.clear(); orphanAnnotations.push_back(std::move(late)); continue; }   // still being written: closed when its write comes back
        const std::string site2 = e.annSite, id = e.annId, token = e.token; auto r = std::make_shared<BA::Outcome>();
        Ann::Run([=] { std::lock_guard<std::mutex> g(Ann::writeLock); *r = resolved ? Ann::Store().Resolve(site2, id, token, "resolved", summary, actor) : Ann::Store().Release(site2, id, token, actor, summary); },
                 [=] { Diag(L"annotation " + W(id) + (resolved ? L": resolved " : L": released ") + W(BA::CodeName(r->code))); Ann::RefreshStatus(); if (Ann::launcher.HistoryVisible()) Ann::LoadHistory(false, false); });
    }
}
std::string LastParagraph(const std::string& text) {
    std::string t = text; while (!t.empty() && (t.back() == '\n' || t.back() == ' ' || t.back() == '\r')) t.pop_back();
    const auto cut = t.rfind("\n\n"); if (cut != std::string::npos && t.size() - cut > 12) t = t.substr(cut + 2);
    return ClipUtf8(ChatPrompt::OneLine(t, 600), 1500);
}

// ---------------------------------------------------------------- one turn
std::shared_ptr<CS::Store> Snapshots() { if (!snapshots) snapshots = std::make_shared<CS::Store>(Ann::DataRoot() + L"\\" + CS::StoreFolderName); return snapshots; }
// SEC-13: the prompt goes only in the mode the browser asked (« Demander » unless the user chose another one). false: held
// (the mode's confirmation sends it: ModeSettled) or the turn ended in error.
bool PromptModeReady() {
    const auto mv = ModesOf(agentId, agentInfo[agentId]);
    switch (ChatOptions::PromptGateFor(mv.current, modeAwaited, modeRefused, In(mv.map.never, mv.current))) {
    case ChatOptions::PromptGate::Go: turn.modeWait = false; return true;
    case ChatOptions::PromptGate::Wait: if (!turn.modeWait) Diag(L"prompt held until the session is in « " + W(modeAwaited) + L" » (now « " + W(mv.current) + L" »)"); turn.modeWait = true; return false;   // i18n-exempt: log detail
    case ChatOptions::PromptGate::Fix: break;
    }
    const std::string want = WantedMode(mv);
    if (turn.modeFixed || want.empty()) {
        Diag(L"prompt refused: the session stays in « " + W(mv.current) + L" » (" + (modeRefused ? L"mode change refused" : L"a mode that asks for nothing") + L")");   // i18n-exempt: log detail
        turn.modeWait = false; Acp::Error e; e.code = Acp::ErrorCode::Protocol; e.detail = "session/set_mode"; turn.failure = e; FinishTurn("error"); return false;
    }
    turn.modeFixed = turn.modeWait = true; SetAgentMode(want); return false;
}
// SEC-13: the session announced its mode (or refused to change it).
void ModeSettled() {
    const auto mv = ModesOf(agentId, agentInfo[agentId]);
    if (!modeAwaited.empty() && mv.current == modeAwaited) modeAwaited.clear();
    if (modeAwaited.empty() && In(mv.map.never, mv.current) && session && sessionReady) {   // SEC-02: the agent went to a mode that asks for nothing
        const std::string want = WantedMode(mv);
        if (!want.empty()) { Diag(L"the session went to « " + W(mv.current) + L" », a mode that asks for nothing: set back to « " + W(want) + L" »"); SetAgentMode(want); }   // i18n-exempt: log detail
    }
    if (turn.active && turn.modeWait && !turn.promptSent) SendPrompt();
}
void ModeRefused(const Acp::Event& e) {
    const auto mv = ModesOf(agentId, agentInfo[agentId]);
    if (modeAwaited.empty() || (e.request == "set_config_option" && !mv.viaOption && mv.map.planOption.empty())) return;   // a model or an effort, not the mode
    modeAwaited.clear(); modeRefused = true;
    if (turn.active && turn.modeWait && !turn.promptSent) { turn.modeWait = false; Diag(L"prompt refused: the mode was not applied"); turn.failure = e.error; if (turn.failure.Ok()) turn.failure.code = Acp::ErrorCode::Protocol; FinishTurn("error"); }   // i18n-exempt: log detail
}
void SendPrompt() {
    if (!turn.active || !session || !sessionReady) return;
    if (!PromptModeReady()) return;
    std::vector<Acp::ContentBlock> blocks;
    if (!convo.preambleSent) {
        ChatPrompt::PreambleInput in; in.site = site.origin; in.folder = U8(folder); in.browser = "the browser";
        blocks.push_back(Acp::ContentBlock::Text(ChatPrompt::Preamble(in))); convo.preambleSent = true; convo.data.head.promptVersion = ChatPrompt::Version;
    }
    if (!convo.recap.empty()) { blocks.push_back(Acp::ContentBlock::Text(convo.recap)); convo.recap.clear(); Diag(L"the conversation so far given to the agent that took the chat over"); }   // R42
    const bool images = agentInfo[agentId].caps.image;
    for (const auto& e : turn.elements) {
        ChatPrompt::ElementInput in; const auto& t = e.pending.target;
        in.url = e.pending.source.url; in.viewportW = t.viewport.width; in.viewportH = t.viewport.height; in.tag = t.tag; in.text = t.selectionText ? *t.selectionText : t.text; in.selector = t.selector;
        in.role = t.role; in.name = t.accessibleName; in.x = t.rect.x; in.y = t.rect.y; in.w = t.rect.width; in.h = t.rect.height;
        for (const auto& s : t.styles) in.styles.emplace_back(s.name, s.value);
        in.spacing = BA::SpacingText(t.spacing); in.components = e.components; in.source = e.source; in.nearest = e.nearest; in.crossOriginFrame = e.sel.frameKind == "cross-origin"; in.hasImage = images && !e.png.empty();
        blocks.push_back(Acp::ContentBlock::Text(ChatPrompt::ElementBlock(in, RandomHex(8))));
        if (in.hasImage) blocks.push_back(Acp::ContentBlock::Image("image/png", Base64(e.png)));
    }
    blocks.push_back(Acp::ContentBlock::Text(turn.text));
    if (images) for (const auto& a : turn.attachments) blocks.push_back(Acp::ContentBlock::Image(a.mime, a.base64));
    stats.sendAt = Ann::NowMs(); stats.firstTextAt = 0;
    if (!session->Prompt(std::move(blocks))) { Acp::Error e; e.code = Acp::ErrorCode::NotReady; turn.failure = e; FinishTurn("error"); return; }
    turn.promptSent = true;
    Diag(L"prompt sent: " + std::to_wstring(turn.elements.size()) + L" element(s), " + std::to_wstring(turn.attachments.size()) + L" image(s)");
}
void BeginSnapshotThenPrompt() {
    if (!turn.active) return;
    struct Out { CS::Result r; std::string id; CS::ScanStats stats; };
    auto out = std::make_shared<Out>(); auto store = Snapshots(); const std::wstring dir = folder; const std::string turnId = turn.id;
    Ann::Run([out, store, dir] { out->r = store->BeginTurn(dir, &out->id, &out->stats); },
             [out, turnId] {
                 if (!turn.active || turn.id != turnId) { if (out->r.ok()) { auto s = Snapshots(); Ann::Run([s] { const std::vector<std::string> none; s->EndTurn(nullptr, nullptr, &none); }, nullptr); } return; }
                 Diag(L"snapshot before the turn: " + W(CS::StatusName(out->r.status)) + L", " + std::to_wstring(out->stats.files) + L" files, " + Ann::Ms(out->stats.milliseconds) + L" ms" + (out->r.detail.empty() ? L"" : L" — " + W(out->r.detail)));
                 turn.snapshot = out->r.ok(); turn.snapshotTurn = out->id;
                 if (out->r.status == CS::Status::TurnOpen) { Problem p; p.title = TR(chat_err_generic_title); p.message = TR(chat_strip_other_window); turn.problem = p; FinishTurn("error"); return; }
                 if (!turn.snapshot) Strip("undo", "neutral", TR(chat_strip_no_undo)); else ClearStrip("undo");
                 if (turn.cancelled) { FinishTurn("cancelled"); return; }
                 SendPrompt();
             });
}
// `batch`: « Traiter les annotations » — `text` is then what the user added (it may be empty); the agent gets the
// batch's instruction (activeBatch, set by StartBatchTurn) and the user's message carries the card.
void StartTurn(const std::string& text, const std::vector<std::string>& elementIds, const std::vector<std::string>& attachmentIds, bool batch) {
    if (turn.active) return;
    if (!Usable()) { Diag(L"send ignored: no project folder or not a local site"); return; }
    turn = Turn{}; turn.active = true; turn.id = "t" + std::to_string(++convo.turnSeq); turn.batch = batch; turn.text = batch ? activeBatch.instruction : text; turn.startedMs = Ann::NowMs();
    if (activeStrips.count("batch")) ClearStrip("batch");
    for (const auto& id : elementIds) if (auto it = composerElements.find(id); it != composerElements.end()) { turn.elements.push_back(std::move(it->second)); composerElements.erase(it); }
    for (const auto& id : attachmentIds) if (auto it = composerAttachments.find(id); it != composerAttachments.end()) { turn.attachments.push_back(std::move(it->second)); composerAttachments.erase(it); }
    if (auto t = ChatTab()) turn.navStamp = t->navStamp;
    auto& head = convo.data.head; const auto now = BA::SystemNow();
    // the user's own words: the request without the chips' labels (they sit in the text where they were typed)
    std::string words = text; for (const auto& e : turn.elements) for (auto at = words.find(e.label); !e.label.empty() && at != std::string::npos; at = words.find(e.label)) words.erase(at, e.label.size());
    while (!words.empty() && (words.back() == ' ' || words.back() == '\n' || words.back() == '\r' || words.back() == '\t')) words.pop_back();
    if (head.title.empty() && batch) head.title = T8(I18n::Id::chat_batch_title);
    if (head.title.empty()) {   // the first request names the conversation
        head.title = ChatPrompt::OneLine(words, 44); if (head.title.empty() && !turn.elements.empty()) head.title = ChatPrompt::OneLine(U8(AH::ElementLabel(turn.elements.front().pending.target)), 44);
    }
    if (head.source.empty()) for (const auto& e : turn.elements) if (e.source.known) { head.source = e.source.Display(); break; }
    {   SJ::Value m = SJ::Value::Obj(); m.Set("id", Str("u" + turn.id)); m.Set("text", Str(text));
        SJ::Value els = SJ::Value::Arr(); for (const auto& e : turn.elements) els.Push(ElementRef(e)); m.Set("elements", std::move(els));
        SJ::Value ats = SJ::Value::Arr(); for (const auto& a : turn.attachments) ats.Push(AttachmentRef(a)); m.Set("attachments", std::move(ats));
        if (batch) {   // the card « Traiter N annotations » (contract B 1.4): the lines, and the exact instruction for « Voir la consigne »
            SJ::Value an = SJ::Value::Obj(); an.Set("batchId", Str(activeBatch.id));
            SJ::Value items = SJ::Value::Arr(); for (const auto& i : activeBatch.items) items.Push(BatchItemJson(i, true));
            an.Set("items", std::move(items)); an.Set("instruction", Str(activeBatch.instruction)); m.Set("annotations", std::move(an));
        }
        m.Set("at", Str(Iso(now))); const std::size_t at = Keep("message.user", m); if (batch) { activeBatch.item = at; activeBatch.userId = "u" + turn.id; }
        Send("message.user", std::move(m)); }
    const auto& info = agentInfo[agentId];
    {   // D6: the header says at once what the composer shows — the choice made before the session (pending), else the
        // agent's current one; the session confirms it later (FillTurnHeader), or corrects it if it announces another
        auto chosen = [](const Acp::ConfigOption* o, const std::string& pending, const std::string& agent) {
            if (o && !pending.empty()) for (const auto& c : o->choices) if (c.value == pending && !ChatOptions::IsAlias(c)) return c.name.empty() ? c.value : c.name;
            return LabelOf(o, agent); };
        SJ::Value m = SJ::Value::Obj(); m.Set("turnId", Str(turn.id)); m.Set("model", Str(chosen(Acp::FindModelOption(info.options), pendingModel, agentId)));
        const std::string effort = chosen(Acp::FindEffortOption(info.options), pendingEffort, ""); if (!effort.empty()) m.Set("effort", Str(effort));
        m.Set("agentId", Str(agentId)); m.Set("at", Str(Iso(now))); turn.startItem = Keep("turn.start", m); Send("turn.start", std::move(m)); }
    if (post) KillTimer(post, AgentIdleTimer);
    SendState(); SyncChromeBits();
    {   SJ::Value m = SJ::Value::Obj(); m.Set("active", SJ::Value::Bool(picking && !Ann::s.paused)); Send("pick.state", std::move(m)); }   // V1.2: a click during the turn still goes to the chat (next message)
    // the element shows who works on it (Paper 6.10): only an element of the top document with a unique selector
    doneMarkSelector.clear(); turn.markSelector.clear();
    for (const auto& e : turn.elements) if (e.sel.frameKind == "main" && e.pending.target.shadowPath.empty() && e.pending.target.selectorUnique && !e.pending.target.selector.empty()) { turn.markSelector = e.pending.target.selector; break; }
    ApplyMark(turn.markSelector.empty() ? "clear" : "working", turn.markSelector);
    if (!turn.elements.empty()) WriteAnnotations(turn.elements, words.find_first_not_of(" \t\r\n") == std::string::npos ? text : words);   // the annotation's note: what the user wrote, not the chips' technical names
    SaveConvo();
    GiveOwedChoices();   // M: a choice still owed to this session (made aside, no turn ended since) — before its prompt, never during a turn
    EnsureSession([] { BeginSnapshotThenPrompt(); });
    Diag(L"turn " + W(turn.id) + L" started");
}
void CompleteTurn(const std::string& stopReason, const CS::TurnSummary* summary, const std::vector<std::pair<std::string, CS::FileDiff>>* diffs = nullptr) {
    const bool failed = stopReason == "error"; const double ms = Ann::NowMs() - turn.startedMs;
    if (stopReason == "end_turn" && !turn.lastPlan.empty() && turn.planPermission.empty() && CurrentModeKind() == "plan") SendPlan(turn.lastPlan, true);   // Paper 6.12: « Plan proposé » waits for « Lancer le plan »
    SJ::Value m = SJ::Value::Obj(); m.Set("turnId", Str(turn.id)); m.Set("stopReason", Str(stopReason == "max_turn_requests" ? std::string("max_tokens") : stopReason)); m.Set("durationMs", SJ::Value::Int(std::int64_t(ms)));
    bool changed = false;
    if (summary) {
        SJ::Value changes = SJ::Value::Arr();
        for (const auto& c : summary->changes) { if (changes.items.size() >= 200) break; SJ::Value o = SJ::Value::Obj(); o.Set("path", Str(c.path)); o.Set("status", Str(std::string(CS::ChangeKindName(c.kind)))); o.Set("added", SJ::Value::Int(c.additions)); o.Set("removed", SJ::Value::Int(c.deletions));
            if (diffs) for (const auto& d : *diffs) if (d.first == c.path) o.Set("diff", DiffJson(c.path, d.second));   // « Voir les changements » (contract B: FileChange.diff)
            changes.Push(std::move(o)); }
        changed = !summary->changes.empty();
        m.Set("changes", std::move(changes));
        if (!summary->checkpointId.empty()) { m.Set("checkpointId", Str(summary->checkpointId)); checkpointOfTurn[turn.id] = summary->checkpointId; }
        m.Set("undoable", SJ::Value::Bool(changed && !summary->checkpointId.empty()));
        m.Set("snapshotTurn", Str(summary->turnId)); snapshotOfTurn[turn.id] = summary->turnId;   // host field: kept with the history for « Tout annuler » after a restart
    } else m.Set("undoable", SJ::Value::Bool(false));
    if (!turn.summaryText.empty()) m.Set("copyText", Str(ClipUtf8(turn.summaryText, 60000)));
    Keep("turn.end", m); Send("turn.end", std::move(m));
    if (failed) { if (!turn.problem.title.empty()) SendError(turn.problem, ""); else { const auto p = ProblemFor(turn.failure, agentId); SendError(p, turn.failure.detail);
            if (turn.failure.code == Acp::ErrorCode::AuthRequired) Strip("auth", "danger", TRF(chat_strip_disconnected, W(Agent().name)), "connect", TR(chat_action_connect)); } }
    const bool done = stopReason == "end_turn";
    const bool planned = done && !changed && CurrentModeKind() == "plan";   // a plan is not a change: the annotation goes back to waiting, with the reason
    CloseAnnotations(turn.elements, done && !planned, done && !planned ? (LastParagraph(turn.summaryText).empty() ? U8(TRF(chat_resolve_default, AgentShort(agentId))) : LastParagraph(turn.summaryText))
                                               : U8(planned ? std::wstring(TR(chat_release_planned)) : stopReason == "cancelled" ? std::wstring(TR(chat_release_stopped)) : std::wstring(TR(chat_release_failed))));
    Diag(L"turn " + W(turn.id) + L" ended: " + W(stopReason) + L" in " + Ann::Ms(ms) + L" ms" + (summary ? L", " + std::to_wstring(summary->changes.size()) + L" file(s) changed (+" + std::to_wstring(summary->additions) + L" -" + std::to_wstring(summary->deletions) + L")" : L", no snapshot"));
    FillTurnHeader();   // D6: kept with the turn (given back after a reload, in the history)
    const std::string markSelector = turn.markSelector; turnNavStamp = turn.navStamp; const bool wasBatch = turn.batch;
    turn = Turn{}; doneMarkSelector = changed && done ? markSelector : std::string();
    GiveOwedChoices();   // M / E3: chosen aside while this turn ran
    SendState(); SyncChromeBits(); SaveConvo();
    if (wasBatch) BatchTurnEnded(stopReason);   // the card's last word: what the store says, and what is left
    {   SJ::Value p = SJ::Value::Obj(); p.Set("active", SJ::Value::Bool(picking && Open() && !Ann::s.paused)); Send("pick.state", std::move(p)); }
    {   std::vector<std::string> paths; if (summary) for (const auto& c : summary->changes) paths.push_back(c.path);
        AfterTurnPage(paths); }   // R36: the transition picks its way from the files changed
    void CheckFolderMatchesPage(); CheckFolderMatchesPage();   // the page's files must be in the folder the agent worked in (else the card says it)
    ArmAgentIdle();
    if (auto next = std::move(afterTurn)) { afterTurn = nullptr; next(); }
    ProcessQueue();
}
void FinishTurn(const std::string& stopReason) {
    if (!turn.active || turn.ending) return;
    turn.ending = true; CloseThought(); waitingPermissions.clear();
    if (!turn.planPermission.empty()) turn.planPermission.clear();
    if (!turn.snapshot) { CompleteTurn(stopReason, nullptr); return; }
    struct Out { CS::Result r; CS::TurnSummary summary; CS::ScanStats stats; std::vector<std::pair<std::string, CS::FileDiff>> diffs; };
    auto out = std::make_shared<Out>(); auto store = Snapshots(); auto agentFiles = std::make_shared<std::vector<std::string>>(turn.agentFiles);
    Ann::Run([out, store, agentFiles] {
            for (int attempt = 0; attempt < 5; ++attempt) { out->r = store->EndTurn(&out->summary, &out->stats, agentFiles.get()); if (out->r.status != CS::Status::Busy) break; Sleep(200); }
            if (out->r.ok() && !out->summary.checkpointId.empty())   // the first files' diffs travel with the turn's end (the others on demand)
                for (const auto& c : out->summary.changes) { if (out->diffs.size() >= 6) break; CS::FileDiff d; if (!c.binary && store->Diff(c.path, out->summary.checkpointId, &d, 64 * 1024).ok()) out->diffs.emplace_back(c.path, std::move(d)); }
        },
             [out, stopReason] {
                 Diag(L"snapshot after the turn: " + W(CS::StatusName(out->r.status)) + L", " + Ann::Ms(out->stats.milliseconds) + L" ms" + (out->r.detail.empty() ? L"" : L" — " + W(out->r.detail)));
                 if (!turn.active) return;
                 CompleteTurn(stopReason, out->r.ok() ? &out->summary : nullptr, &out->diffs);
             });
}
void StopTurn() {
    if (!turn.active) return;
    // M1 (review R): an agent that does not answer « cancel » — a second « Arrêter », or 5 s, ends the turn anyway
    if (turn.cancelled) { if (post) KillTimer(post, StopTimer); DisposeSession(L"stop not answered (second stop)"); if (turn.active) FinishTurn("cancelled"); return; }   // i18n-exempt: log reason
    if (post) SetTimer(post, StopTimer, 5000, nullptr);
    turn.cancelled = true;
    if (turn.promptSent && session) session->Cancel();          // -> TurnEnded{cancelled}
    else if (!sessionReady && !sessionStarting && !preparing) FinishTurn("cancelled");
    else if (sessionStarting || preparing) { afterReady = nullptr; if (prepareCancel) *prepareCancel = true; FinishTurn("cancelled"); }
    else if (turn.modeWait) FinishTurn("cancelled");   // SEC-13: held for the mode, never sent
    // else: the snapshot is being taken; BeginSnapshotThenPrompt ends the turn
}
void ProcessQueue() {
    if (!turn.active && queue.empty()) TakeShownAside();   // C2: nothing waits — the conversation shown becomes the engine's
    if (turn.active || queue.empty()) { SendQueue(); return; }
    // Mode Commentaire: the next message may belong to another element's conversation — it becomes the engine's first
    if (const std::string c = queue.front().convId; !c.empty() && c != convo.data.head.id && !SwitchEngineTo(c)) { queue.pop_front(); ProcessQueue(); return; }
    Queued q = std::move(queue.front()); queue.pop_front(); SendQueue(); RefreshWaitStrip();
    if (q.annotations.empty()) AttachQueuedAnchor(q);   // queue #63: queued before its element was ready — its element (image, source) joins it now
    if (!q.annotations.empty()) { StartBatchTurn(q.text, q.batchId, q.annotations); if (!turn.active) ProcessQueue(); return; }   // a batch replaced since is dropped: the next one runs
    StartTurn(q.text, q.elements, q.attachments);
}

// ---------------------------------------------------------------- undo (contract C)
void Undo(const std::string& panelTurn, const std::string& checkpoint) {
    auto fail = [&](I18n::Id why) { SJ::Value m = SJ::Value::Obj(); m.Set("turnId", Str(panelTurn)); m.Set("ok", SJ::Value::Bool(false)); m.Set("message", Str(T8(why))); Send("turn.undone", std::move(m)); };
    if (turn.active) { fail(I18n::Id::chat_undo_busy); return; }
    if (folder.empty()) { fail(I18n::Id::chat_undo_gone); return; }
    std::string snapshotTurn; if (checkpoint.empty()) { auto it = snapshotOfTurn.find(panelTurn); if (it == snapshotOfTurn.end()) { fail(I18n::Id::chat_undo_gone); return; } snapshotTurn = it->second; }
    auto out = std::make_shared<CS::Result>(); auto store = Snapshots(); const std::wstring dir = folder;
    Ann::Run([out, store, dir, snapshotTurn, checkpoint] {
            *out = store->Open(dir); if (out->status != CS::Status::Ok && out->status != CS::Status::Conflict) return;
            *out = checkpoint.empty() ? store->UndoTurn(snapshotTurn) : store->Restore(checkpoint);
        },
        [out, panelTurn, checkpoint] {
            Diag(std::wstring(checkpoint.empty() ? L"undo turn: " : L"restore checkpoint: ") + W(CS::StatusName(out->status)) + L", " + std::to_wstring(out->restored.size()) + L" file(s) put back" + (out->detail.empty() ? L"" : L" — " + W(out->detail)));
            // every panel turn whose snapshot turn was undone
            std::vector<std::string> turns; for (const auto& id : out->undoneTurns) for (const auto& p : snapshotOfTurn) if (p.second == id) turns.push_back(p.first);
            if (turns.empty() && !panelTurn.empty()) turns.push_back(panelTurn);
            const bool ok = out->ok();
            for (const auto& t : turns) {
                SJ::Value m = SJ::Value::Obj(); m.Set("turnId", Str(t)); m.Set("ok", SJ::Value::Bool(ok));
                if (!ok) {
                    if (out->status == CS::Status::Conflict) { SJ::Value cs = SJ::Value::Arr(); for (const auto& c : out->conflicts) { if (cs.items.size() >= 30) break; cs.Push(Str(c.path)); } m.Set("conflicts", std::move(cs)); m.Set("message", Str(T8(I18n::Id::chat_undo_conflict))); }
                    else m.Set("message", Str(T8(out->status == CS::Status::NotFound || out->status == CS::Status::AlreadyUndone ? I18n::Id::chat_undo_gone : out->status == CS::Status::TurnOpen ? I18n::Id::chat_undo_busy : I18n::Id::chat_err_undo)));
                } else for (auto& item : convo.data.items) { const auto* ty = item.Get("type"); const auto* id = item.Get("turnId"); if (ty && ty->string == "turn.end" && id && id->string == t) { item.Set("undoable", SJ::Value::Bool(false)); item.Set("undone", SJ::Value::Bool(true)); } }
                Send("turn.undone", std::move(m));
            }
            if (ok) { SaveConvo(); doneMarkSelector.clear(); ApplyMark("clear", ""); if (auto t = ChatTab()) turnNavStamp = t->navStamp; AfterTurnPage(out->restored); }
            if (auto next = std::move(afterUndo)) { afterUndo = nullptr; next(ok); }
        });
}
// M7 (review R): the checkpoint of a turn of the conversation SHOWN — aside, its own turns (their ids are the engine's too)
std::string CheckpointOf(const std::string& turnId) {
    if (!aside.on) { const auto it = checkpointOfTurn.find(turnId); return it == checkpointOfTurn.end() ? std::string() : it->second; }
    for (const auto& item : aside.c.data.items) { const auto* ty = item.Get("type"); const auto* id = item.Get("turnId"); const auto* cp = item.Get("checkpointId");
        if (ty && ty->IsString() && ty->string == "turn.end" && id && id->IsString() && id->string == turnId && cp && cp->IsString()) return cp->string; }
    return "";
}
void ViewChanges(const std::string& panelTurn, const std::string& path) {
    const std::string cpId = CheckpointOf(panelTurn); if (cpId.empty() || folder.empty()) return;   // M7: of the conversation shown
    std::vector<std::string> files;
    if (!path.empty()) files.push_back(path);
    else for (const auto& item : ShownItems()) { const auto* ty = item.Get("type"); const auto* id = item.Get("turnId"); const auto* ch = item.Get("changes");
        if (ty && ty->string == "turn.end" && id && id->string == panelTurn && ch && ch->IsArray()) for (const auto& c : ch->items) if (const auto* p = c.Get("path"); p && p->IsString() && files.size() < 20) files.push_back(p->string); }
    if (files.empty()) return;
    struct Out { std::vector<std::pair<std::string, CS::FileDiff>> diffs; }; auto out = std::make_shared<Out>(); auto store = Snapshots(); const std::wstring dir = folder; const std::string checkpoint = cpId;
    Ann::Run([out, store, dir, files, checkpoint] {
            if (!store->HasProject()) store->Open(dir);
            for (const auto& f : files) { CS::FileDiff d; if (store->Diff(f, checkpoint, &d, 96 * 1024).ok()) out->diffs.emplace_back(f, std::move(d)); }
        },
        [out, panelTurn] {
            SJ::Value arr = SJ::Value::Arr(); for (const auto& d : out->diffs) arr.Push(DiffJson(d.first, d.second));
            SJ::Value m = SJ::Value::Obj(); m.Set("turnId", Str(panelTurn)); m.Set("diffs", std::move(arr)); Send("changes.diff", std::move(m));
        });
}

} // namespace Chat

// « Ton agent » (design/ton-agent-interface.md): the agents' status starts with the browser, its work stops with it.
void ChatAgentsStartup() { Chat::EnsureAgentStatus(); }
void ChatAgentsShutdown() { AgentStatus::Shutdown(); }
