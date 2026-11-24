#!/usr/bin/env python3
"""Host contracts read from the source (review R, design/chat-final-review.md): what the host must do on a page message,
checked on the code itself, comments removed — a call left in a comment (C1) fails here.

    python -I tests/chat/host_contract_test.py      (also run by tests/chat/run_tests.py)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'src'
checks = []
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding='utf-8')
    except Exception:
        pass


def check(ok, what, detail=''):
    checks.append(bool(ok))
    print(('PASS ' if ok else 'FAIL ') + what + (' — ' + str(detail) if detail and not ok else ''))


def code(name):
    """The file without comments (// to the end of the line, /* */), strings kept."""
    t = (SRC / name).read_text(encoding='utf-8')
    out, i, n = [], 0, len(t)
    while i < n:
        c = t[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == '\\' else 1
            out.append(t[i:j + 1]); i = j + 1; continue
        if t.startswith('//', i):
            j = t.find('\n', i); i = n if j < 0 else j; continue
        if t.startswith('/*', i):
            j = t.find('*/', i + 2); i = n if j < 0 else j + 2; continue
        out.append(c); i += 1
    return ''.join(out)


def block_after(text, marker):
    """The braces block that starts at the first '{' after `marker`."""
    at = text.find(marker)
    if at < 0:
        return ''
    s = text.find('{', at); depth = 0
    for j in range(s, len(text)):
        depth += text[j] == '{'; depth -= text[j] == '}'
        if depth == 0:
            return text[s:j + 1]
    return ''


ui = code('chat-host-ui.h')

# C1: on « ready » the page gets hello, its layout, then the state, the thread, the commands and the queue (contract §2)
ready = block_after(ui, 'if (type == "ready")')
order = [ready.find(x) for x in ('Send("hello"', 'SendLayout(', 'SendState();', 'SendThread();', 'SendCommands();', 'SendQueue();')]
check(ready and all(p >= 0 for p in order) and order == sorted(order),
      'C1: « ready » sends hello, layout, state, thread, commands, queue — in that order, none left in a comment', order)

turn = code('chat-host-turn.h'); batch = code('chat-host-batch.h'); comment = code('chat-comment.h')

# C2: a conversation shown « aside » while nothing runs becomes the engine's before anything is sent from it, and at the
# end of a turn when nothing waits for another conversation
soq = block_after(ui, 'void SendOrQueue(')
check(0 <= soq.find('TakeShownAside()') < soq.find('StartTurn('), 'C2: SendOrQueue takes the conversation shown aside before starting a turn')
check('TakeShownAside()' in block_after(turn, 'void ProcessQueue() {'), 'C2: the end of a turn (ProcessQueue, nothing queued) gives the engine to the conversation shown')
check('TakeShownAside()' in block_after(batch, 'void StartBatchTurn('), 'C2: a batch sent from a bubble aside goes to its conversation')
check('TakeShownAside()' in block_after(ui, 'void KeepAnnotation('), 'C2: Ctrl+Entrée in a bubble aside keeps the line in its conversation')
check('SwitchEngineTo(' in block_after(comment, 'bool TakeShownAside()'), 'C2: by the same path as a queued message (SwitchEngineTo)')

# C3: an element not found never closes the bubble while its conversation works or waits (a turn, an authorization, a
# queued message); it stays at its last place and follows the element again if it comes back. Opened from the
# launcher's history (element gone), the bubble follows no selector: it cannot close itself
on_anchor = block_after(comment, 'void OnAnchor(')
gone = block_after(on_anchor, 'if (e.gone)')
check('BubbleBusy()' in gone and gone.find('BubbleBusy()') < gone.find('CloseBubble('), 'C3: « gone » closes only when nothing works or waits for this conversation', gone[:160])
check('bubble.lost' in on_anchor, 'C3: an element found again is followed again (bubble.lost)')
busy = block_after(comment, 'bool BubbleBusy()')
check(all(x in busy for x in ('turn.active', 'waitingPermissions', 'HasQueued(')), 'C3: busy = a turn of its conversation, an authorization waiting, or a message queued for it')
check('bubble.noFollow' in block_after(comment, 'void FollowBubbleElement(bool current) {') and 'noFollow = true' in block_after(comment, 'bool OpenFromHistory(const std::wstring& id) {'),
      'C3: a conversation opened from the history follows no element (it used to close at once)')

# E2 / M9 (coordinator): the model, the effort and the mode chosen are remembered per site and given to EVERY new session
apply = block_after(turn, 'void ApplyDefaults() {')
check(apply.count('ChoiceForNewSession(') >= 2 and 'RememberedChoice(' in apply, 'E2 / M9: ApplyDefaults gives each new session the choice made, else the one remembered for the site (model and mode)')
set_mode = block_after(ui, 'if (type == "setMode")')
check('RememberSetting(&ChatStore::Project::mode, id)' in set_mode, 'M9: a mode chosen is remembered for the site')

# M (Q's monkey 0.3.561, seed 1 #78-#81: « Auto » chosen in the panel while another element's turn waited — every bubble
# and the panel then showed « Demander »): the mode, the model and the effort chosen are kept between the panel and the
# bubbles. A conversation shown aside shows what its own session will get, never the other element's session; the
# session a choice is owed to gets it when its turn ends; a choice made before a session is the site's once it starts.
host = code('chat-host.h'); state = block_after(host, 'void SendState() {')
check(state.count('ChatOptions::ShownSetting(!aside.on, (choicesOwed & ') == 3 and all(x in state for x in ('OwedMode', 'OwedModel', 'OwedEffort', 'NewSessionModel(model)', 'NewSessionEffort(effort)', 'WantedMode(mv)')),
      'M: the state shows the mode, the model and the effort of the conversation shown (aside: what its session will get)')
remember = block_after(turn, 'void RememberSetting(')
check('projects.sites[site.origin].*field = id; RememberChoices();' in remember and 'projects.sites.count(site.origin)' in remember, 'M: one way to remember a choice for the site (panel or bubble)')
for t, f, bit in (('setModel', 'model', 'OwedModel'), ('setEffort', 'effort', 'OwedEffort'), ('setMode', 'mode', 'OwedMode')):
    h = block_after(ui, 'if (type == "' + t + '")')
    check('RememberSetting(&ChatStore::Project::' + f + ', id)' in h and 'choicesOwed |= ' + bit in h and '.sites[site.origin].' not in h,
          'M: « ' + t + ' » remembers the choice for the site wherever it is made; aside, the running session is owed it')
check(all('RememberSetting(&ChatStore::Project::' + f + ', pending' in apply for f in ('model', 'effort', 'mode')) and 'choicesOwed = 0;' in apply,
      'M: a choice made before the session (no folder yet, another provider) is the site\'s once the session starts')
owed = block_after(turn, 'void GiveOwedChoices() {')
check(all(x in owed for x in ('NewSessionModel(o)', 'NewSessionEffort(e)', 'WantedMode(mv)', 'SetAgentMode(want)', 'choicesOwed = 0;')), 'M / E3: the choices made aside are given to the session whose turn ran')
done = block_after(turn, 'void CompleteTurn(')
check(0 <= done.find('turn = Turn{};') < done.find('GiveOwedChoices();') < done.find('SendState();'), 'M / E3: …when that turn has ended (never during it), before the state is sent')
check('choicesOwed = 0;' in block_after(turn, 'void DisposeSession(const wchar_t* why) {'), 'M: nothing is owed to a session that left')
# M (coordinator): « Lancer le plan » — the mode that follows Plan is the user's choice (the next element does not plan again)
plan_run = block_after(ui, 'if (type == "plan.run")')
check(0 <= plan_run.find('turn.planLaunched = true;') < plan_run.find('session->RespondPermission(turn.planPermission') and 'RememberSetting(&ChatStore::Project::mode, ChatOptions::ModeAfterPlan(true, next' in plan_run,
      'M: « Lancer le plan » keeps the mode that follows (the one the browser sets; the agent\'s own, once it says it)')
check('ChatOptions::PlanRunOption(turn.planOptions, mv.map.ask, mv.map.automatic, mv.map.never)' in plan_run and 'planOptions.back()' not in plan_run and 'rfind("allow", 0)' not in plan_run,
      'SEC-02: « Lancer le plan » answers the gate by id with the option that keeps the permissions (never the first allow*, never the last option)')
send_prompt = block_after(turn, 'void SendPrompt() {')
check(0 <= send_prompt.find('if (!PromptModeReady()) return;') < send_prompt.find('session->Prompt('), 'SEC-13: no prompt before the session is in the mode the browser asked')
def case_of(text, marker):
    at = text.find(marker)
    return text[at:text.find('break;', at)] if at >= 0 else ''
check('ModeSettled();' in case_of(turn, 'case T::ModesChanged:') and 'ModeSettled();' in case_of(turn, 'case T::ConfigOptionsChanged:') and 'ModeRefused(e);' in case_of(turn, 'case T::RequestFailed:'),'SEC-13: the mode\'s confirmation sends the held prompt, its refusal ends the turn')
set_folder = block_after(ui, 'void SetFolder(')
check('it->second.mode.clear();' in set_folder and set_folder.find('it->second.mode.clear();') < set_folder.find('RememberChoices();'), 'SEC-08: another folder behind the same origin does not inherit the mode remembered for the old one')
folder_decided = block_after(ui, 'void FolderDecided(')
check('if (same || !sure) return;' not in folder_decided and 'if (!sure) {' in folder_decided and 'folderAsk = true;' in block_after(folder_decided, 'if (!sure) {'), 'SEC-08: an unsure detection of another folder asks the folder question instead of keeping the old folder silently')
check('CS::MayHoldSecrets(FileName(t.diffs.front().path))' in block_after(turn, 'SJ::Value ToolJson(') and 'd.Set("lines", SJ::Value::Arr())' in block_after(turn, 'SJ::Value ToolJson('),
      'SEC-14: a step kept with the conversation never carries the lines of a file that may hold secrets')
append_text = block_after(turn, 'void AppendText(')
check('t->string += more;' in append_text and 'MaxKeptText' in append_text and '+ more)' not in append_text, 'PERF-20: streamed text is appended in place and capped (no copy of the whole message per chunk)')
keep_plan = block_after(turn, 'void KeepModeAfterPlan() {')
check(all(x in keep_plan for x in ('turn.planLaunched', 'ChatOptions::ModeAfterPlan(true, mv.current', 'RememberSetting(&ChatStore::Project::mode, kept)', 'std::none_of(mv.list')), 'M: the mode the agent goes to after its plan is accepted is remembered for the site (only a mode the chat offers)')
events = block_after(turn, 'void OnEvent(const Acp::Event& e) {')
check('info.modes = e.modes; KeepModeAfterPlan(); SendState();' in events and 'info.options = e.configOptions; KeepModeAfterPlan(); SendState();' in events, 'M: …whether the agent says it as a mode or as an option (Codex)')
start_turn = block_after(turn, 'void StartTurn(const std::string& text, const std::vector<std::string>& elementIds, const std::vector<std::string>& attachmentIds, bool batch) {')
check(0 <= start_turn.find('GiveOwedChoices();') < start_turn.find('EnsureSession('), 'M: a choice still owed is given before the next prompt of that session (what the composer shows is what the message runs with)')
check('ChoiceForNewSession(' in block_after(turn, 'std::string WantedMode('), 'M9: the mode shown before the session (R28) is the one it will get')

# E3: a bubble shown aside (another element's turn runs) never changes the running session; its choice waits for its own
for t in ('setModel', 'setEffort', 'setMode'):
    h = block_after(ui, 'if (type == "' + t + '")')
    check('aside.on' in h and h.find('aside.on') < h.find('session->') if 'session->' in h else 'aside.on' in h,
          'E3: « ' + t + ' » from a bubble aside keeps the choice for that conversation, the running session untouched')
check('aside.on' in block_after(ui, 'void MarkSwitch('), 'E3: the « Passé à … » mark goes into the conversation shown aside')

# E1: the click's early « selecting » gave a selector; when the full « select » says it is not a safe one (iframe, shadow,
# not unique), the bubble takes the real one — or follows the current element
take = block_after(comment, 'bool TakeCommentSelection(')
done = block_after(take, 'if (early.on && early.seq == Ann::s.sel.seq')
check('selectorUnique' in done and 'FollowBubbleElement(' in done, 'E1: the full selection corrects the selector the early opening followed')

# E5: a conversation already being read is not read again — one reading, one opening (the latest « then » wins)
show = block_after(comment, 'void ShowConversation(const std::string& id, std::function<void()> then')
check('loadingConv' in show and show.find('loadingConv') < show.find('Ann::Run('), 'E5: a second ShowConversation of the same conversation while it is read does not read it again')

# E4: every placement of an opening carries its window; framed again only when the surface does not fit
place = block_after(comment, 'void PlaceBubble(bool scrolling) {')
check('FrameKept(' in place and place.find('FrameKept(') < place.find('FrameBubble('), 'E4: PlaceBubble carries the frame of the opening before framing again')
check('m.Set("room"' in block_after(comment, 'void SendLayout(bool openNow) {'), 'E4 (b): the layout tells the page the room really usable for its menus (page area)')

# M1: « Arrêter » the agent does not answer: 5 s later (or at a second « Arrêter ») the session is stopped and the turn ends
stop = block_after(turn, 'void StopTurn() {')
check('StopTimer' in stop and 'turn.cancelled' in stop[:stop.find('turn.cancelled = true')] if 'turn.cancelled = true' in stop else False,
      'M1: StopTurn arms a guard delay and a second « Arrêter » forces the end')
check('StopTimer' in ui and 'stop not answered' in ui, 'M1: at the guard delay, the session is stopped and the turn ended (cancelled)')
# M10: in the bubble the hover timer runs and the pointer is compared with the bubble's region, not the whole window
hover = ui[ui.find('if (w == HoverTimer)'):][:400]
check('BubbleOpen()' in hover, 'M10: the hover timer keeps running in the bubble (it stopped at !Open())')
check('BubbleRegionScreen()' in block_after(ui, 'bool PointerInPanel() {'), 'M10: the pointer is compared with the bubble region (surface + overflow asked), not the transparent window')
# M11: the browser closed during a turn writes its end (interrupted), so it never shows « Travaille… » again
shut = block_after(ui, 'void Shutdown() {')
check('turn.active' in shut and '"turn.end"' in shut, 'M11: Shutdown ends the running turn in the conversation saved')
# M13: a turn whose session cannot start ends in error instead of waiting forever
ens = block_after(turn, 'void EnsureSession(std::function<void()> ready) {')
check('FinishTurn(' in ens, 'M13: EnsureSession ends the turn in error when no session can start')

# M3: after a turn that changed only CSS (the values glide, no reload), the mark « en cours » becomes « terminé »
atp = block_after(ui, 'void AfterTurnPage(')
check(atp.count('ChatTransition::Reload(') >= 2 and atp.count('GlidedDone') >= 2 and 'Outcome::Glided' in ui, 'M3: every ChatTransition::Reload tells how it ended; a glide puts the « done » mark')

# M2 / F4 (P's contract): a real close tells the page (bubble.hidden: surface invisible, inert, aria-hidden, thread emptied);
# an opening shows the region only at the page's acknowledgement (never the old thread for a few images)
close = block_after(comment, 'void CloseBubble(bool focusPage) {')
check('Send("bubble.hidden"' in close, 'M2 / F4: every real close sends bubble.hidden')
check('Send("bubble.hidden"' not in block_after(comment, 'void BubbleAway('), 'M2: not when another application only hides it for a moment')
check('RevealBubble(L"with the opening layout")' not in block_after(comment, 'void OpenBubble('), 'M2: the opening shows its region at the acknowledgement only')
# M4 (a): back from another application, the bubble that had the keyboard gets it again
back = block_after(comment, 'void BubbleBack(')
check('hadKeyboard' in back and 'KeyboardToBubble(' in back and 'view.Focus()' in block_after(comment, 'void KeyboardTry() {'), 'M4 (a): BubbleBack gives the keyboard back to the bubble that had it (checked and retried)')
check('WA_ACTIVE' in block_after(comment, 'LRESULT CALLBACK BubbleProc('), 'M4 (a): an activated bubble gives its page the keyboard')
# M5: a click while the panel slides out: the slide ends first, and its end never hides the bubble's view
check('PanelState::Closing' in block_after(comment, 'void OpenBubble('), 'M5: OpenBubble finishes the panel closing first')
check('BubbleOpen()' in block_after(ui, 'void FinishClose() {'), 'M5: FinishClose does not hide the view the bubble now shows')
# M15: the height offered is measured again when the page area changes size
check('roomAreaH' in block_after(comment, 'void PlaceBubble(bool scrolling) {'), 'M15: a new page area size measures the height offered again')
# M16: Commentaire entered: the chat's tab is set at once (a reload before the first click keeps the mode)
check('PrepareChat(' in block_after(ui, 'void ModeEntered() {'), 'M16: ModeEntered in Commentaire prepares the chat (its tab)')
# F3: « Nouveau chat » from an error card while a turn runs: stopped first, as the composer's « Nouveau chat »
errnew = ui[ui.find('action == "newChat"'):][:300]
check('turn.active' in errnew, 'F3: an error card\'s « Nouveau chat » waits for the running turn like the composer\'s')
# F7: the bubble's WebView refused: the bubble closes instead of staying « open » and empty
check('CloseBubble(' in ui[ui.find('h.created = '):][:400], 'F7: a WebView that cannot be created closes the bubble')
# F8: after a page change, a bubble whose conversation belongs to another page closes
check('anchorPage' in block_after(comment, 'void ScriptReady() {'), 'F8: ScriptReady closes a bubble whose page is not the one shown')

# M6 / F5: a bubble closed with nothing sent (or the panel chosen) leaves no conversation tied to its element, and frees its image
forget = block_after(comment, 'void ForgetUnsent(')
check('CloseBubble' and 'ForgetUnsent(' in block_after(comment, 'void CloseBubble(bool focusPage) {'), 'M6: closing a bubble with nothing sent forgets its element')
check('ForgetUnsent(' in block_after(comment, 'void SwitchLayout('), 'M6: so does choosing the panel')
check('anchorSelector.clear()' in forget and 'anchorElements.erase(' in forget and 'SentConvo(' in forget, 'M6 / F5: only when nothing was sent — the anchor cleared, the element\'s image freed')
# M7: messages of a bubble shown aside are resolved on ITS conversation
check('ShownItems()' in ui[ui.find('if (type == "retry")'):][:500], 'M7: « Réessayer » takes the message of the conversation shown')
check('ShownItems()' in block_after(turn, 'void ViewChanges(') and 'CheckpointOf(' in block_after(turn, 'void ViewChanges('), 'M7: the changes of a turn of the conversation shown')
check('aside.on' in block_after(ui, 'void KeepAnnotation('), 'M7: Ctrl+Entrée in a bubble aside keeps the line in its conversation')
# M8: the modes are cached with the options (the mode button is there from the first opening)
check('"modes"' in block_after(ui, 'void SaveOptionsCache() {') and '"modes"' in block_after(ui, 'void LoadOptionsCache() {'), 'M8: the modes are cached and read back')
# M14: a held message never waits forever (2 s, or the page reloaded), and a second one does not erase the first
hold = block_after(comment, 'bool HoldForAnchor(')
check('held.on' in hold and 'ReleaseHeld(' in hold and 'HeldTimer' in hold, 'M14: a second send releases the first; a 2 s guard')
check(ui.count('aiming.clear(); ReleaseHeld()') >= 2 or ui.count('ReleaseHeld();') >= 2, 'M14: when the page reloads (aiming emptied) the held message leaves without image')
# F6: the page keeps a bubble's draft per element before the first send
check('"elementKey"' in block_after(comment, 'void SendLayout(bool openNow) {'), 'F6: the layout says the element\'s stable key')
# F9: « Voir » scrolls to the element before the bubble is placed
check('L"peek"' in block_after(comment, 'void ShowEngineWork() {'), 'F9: « Voir » brings the element on screen first')
# F11: a position that came while waiting for the composition waits for the next one (generations)
fl = block_after(comment, 'void FollowLater() {')
check('gen' in fl and 'gen' in block_after(comment, 'LRESULT CALLBACK BubbleProc('), 'F11: the scroll follow places the position its wait was for')

# Q r44: the host's PageInsetCapCss is the page's CAP (panel.js applyInset) — the same number, or the surface is drawn away
import re as _re
cap_page = _re.search(r'CAP = (\d+)', (ROOT / 'assets' / 'chat-panel' / 'panel.js').read_text(encoding='utf-8'))
cap_host = _re.search(r'PageInsetCapCss = (\d+)', code('chat-bubble.h'))
check(cap_page and cap_host and cap_page.group(1) == cap_host.group(1), 'Q r44: PageInsetCapCss (host) == CAP (page)', (cap_host and cap_host.group(1), cap_page and cap_page.group(1)))

# Q pass 1, defect 1 (the user's journey): a click on B while A's bubble is open opens B — it no longer only closes A
take1 = block_after(comment, 'bool TakeCommentSelection(')
check('a click elsewhere closed the bubble' not in take1 and 'if (!bubble.open && Ann::NowMs() - bubble.closedAt < 400)' in take1,
      'Q 1: a click on another element while a bubble is open opens that element\'s bubble (only a click right after closing does nothing)')
# Q pass 1, defect 4 (M4): back in front from another application, the bubble that had the keyboard gets it — even when it was not hidden
fg = block_after(comment, 'void CALLBACK ForegroundChanged(')
check('cameBack' in fg and 'KeyboardToBubble(' in fg and 'MouseDown()' in fg, 'Q 4: back from another application (not a click in the page), the open bubble gets the keyboard')
# Q pass 1, defect 6 (E1): an element in an iframe or a web component finds its conversation again by a stable key
check('anchorKey' in take1 and 'UnsafeAnchorKey(' in take1, 'Q 6: an element without a safe selector is found again by its key')

# monkey seed 1 step 5 (click on a.btn-primary -> bubble on section.hero): a click = the element clicked, always; the host
# logs what the page sent (selector, click point) and never puts the bubble on another element than the click's
early_fn = block_after(comment, 'bool OpenEarly(')
check('env.selector' in early_fn and 'env.px' in early_fn, 'monkey: the early opening logs the clicked selector and the click point')
done2 = block_after(block_after(comment, 'bool TakeCommentSelection('), 'if (early.on && early.seq == Ann::s.sel.seq')
check('bubble.selector = safe' not in done2 and 'not the clicked one' in done2, 'monkey: the full selection never moves the bubble to another element (only says the selector is not safe)')

# M4 (Q's real window): Échap in the browser's own window (its shortcut) closes an open bubble before it would quit the mode
mode = code('annotation-mode.h')
ex = mode[mode.find('if (cmd == AnnotateExitCommand)'):][:200]
check('Chat::BubbleOpen()' in ex and ex.find('CloseBubble') < ex.find('Ann::Exit'), 'M4: the browser shortcut Échap shortcut closes the bubble first')

# M15 (Q's real window): the page area got smaller (the window resized) and the bubble is no longer whole in it — placed again
# by the opening's rules (R44); R29 (never moved) is for a scroll, not for a window that changes size
pl = block_after(comment, 'void PlaceBubble(bool scrolling) {')
check('anchorArea' in pl and 'FullyVisible(' in pl and pl.find('anchorArea') < pl.find('ChatBubble::Place('), 'M15: a smaller page area places a bubble that is no longer whole again')

# M4 panel (Q's monkey): « Chat à droite », back from another application, the panel's composer gets the keyboard again
fgc = block_after(comment, 'void CALLBACK ForegroundChanged(')
check('Open()' in fgc and fgc.find('Open()') < fgc.find('if (!bubble.open'), 'M4 panel: back in front, the open panel gets the keyboard (before the bubble-only checks)')

# monkey seed 3 #63 / #70: a message sent from a bubble is visible at once in THAT bubble — never held out of sight
hold = block_after(comment, 'bool HoldForAnchor(')
check('turn.active' in hold and hold.find('turn.active') < hold.find('held = HeldSend'), 'queue #63: another turn runs — the message is queued at once (visible), not held for its image')
check('SendQueue()' in hold, 'queue #70: a held message is shown as waiting in its bubble')
check('HeldFor(' in block_after(code('chat-host.h'), 'void SendQueue() {'), 'queue: the held message is in the queue the page shows')
pq = block_after(turn, 'void ProcessQueue() {')
check('AttachQueuedAnchor(' in pq and 'anchorElements' in block_after(comment, 'void AttachQueuedAnchor('), 'queue: a queued message gets its element (image, source) when its turn starts')
check('"held"' in block_after(ui, 'if (type == "queue.remove")') or 'CancelHeld(' in ui, 'queue: a held message can be cancelled (its cross)')
# M6 (#86): the panel shown after a bubble closed with nothing sent never keeps that bubble's title
check('SendState()' in block_after(comment, 'void ForgetUnsent('), 'M6 #86: the panel is told the conversation is no longer the element\'s (title)')

# Placement = Paper (Q's invariant: +12 / -4 at a side, 12 px inside a corner; never slid to keep room)
bub = code('chat-bubble.h'); place = bub[bub.find('inline Placement Place('):bub.find('inline bool SentItem(')]   # (its signature has a « {} » default)
check('element.top - lead' in place and 'element.right + gap' in place and 'whole(planned(p))' in place and 'slid' in place, 'Paper: the side positions are exact (+gap, top - lead), a side only if whole there; a clamp only for the inside fallback')
consts = code('chat-comment.h')
check('BubbleGapCss = 12' in consts and 'BubbleLeadCss = 4' in consts, 'Paper: gap 12, lead 4 (CSS px)')
check('OfferedRoomCss(' not in block_after(comment, 'void PlaceBubble(bool scrolling) {'), 'R29 (the user): natural height, never bounded by the room under the fixed edge')

# R47 (coordinator's decision): an authorization (or an error waiting for an action) whose buttons are out of the visible area
# — the SITE's page scrolls softly just enough, once, when it arrives; not while the user scrolls; reduced motion respected
check('RevealCardSoon()' in turn[turn.find('permission asked'):][:600] or 'RevealCardSoon()' in block_after(turn, 'void OnPermission('), 'R47: a permission asked asks for its card to be shown')
check('RevealCardSoon()' in block_after(code('chat-host.h'), 'void SendError('), 'R47: an error waiting for an action too')
rv = block_after(comment, 'void RevealCard() {')
check('RevealScrollCss(' in rv and 'scrollBy' in rv and 'Reduced()' in rv and 'lastScrollAt' in rv, 'R47: the page scrolls by what is missing, softly (or at once with reduced motion), never while the user scrolls')
check('RevealCard()' in block_after(comment, 'void OnBubbleSize('), 'R47: checked at the bubble\'s next size (the card is in it)')

# Decision: the opening height = the conversation's last height (saved with it), else 520 when it has content
pl2 = block_after(comment, 'void PlaceBubble(bool scrolling) {')
ph = block_after(comment, 'int PlannedHeightPx(float k) {')
check('PlannedHeightPx(' in pl2 and 'bubbleHeight' in ph and 'data.items.empty()' in ph, 'decision: PlaceBubble plans with the conversation\'s last height (else 520 with content)')
check('bubbleHeight' in block_after(comment, 'void OnBubbleSize('), 'decision: the height of the conversation is kept with it')
check('PanelKeyboardBack(' in block_after(comment, 'void CALLBACK ForegroundChanged('), 'M4 panel (2): the keyboard back to the panel is delayed and repeated (after a chip)')

# real window (0.3.539): the saved height never shrinks; « has content » is the bubble's own conversation; an authorization's
# card that grows after it arrived is still brought on screen
osz = block_after(comment, 'void OnBubbleSize(')
check('std::max(head.bubbleHeight' in osz, 'decision: the height kept with a conversation never shrinks (the composer-only first size)')
check('ShownItems()' not in block_after(comment, 'int PlannedHeightPx(float k) {').split('return mine')[0].split('AnchoredOf')[1] if 'AnchoredOf' in block_after(comment, 'int PlannedHeightPx(float k) {') else False, 'decision: « has content » is the bubble\'s own conversation')
check('waitingPermissions.empty()' in block_after(comment, 'void RevealCard() {'), 'R47: the card stays due while the authorization waits (it grows after it arrives)')

# real window (0.3.539): « the user is scrolling » = the page's scroll offset changed (not the page reflowing)
check('lastSy' in block_after(comment, 'void OnAnchor('), 'R47: a scroll is an offset change, not a reflow (the reveal was skipped during the agent\'s edit)')

check('bubble slid' in block_after(comment, 'void PlaceBubble(bool scrolling) {'), 'decision: « bubble slid N px along its side to be fully visible » in the log')

# R39 (Q, 0.3.541): the follow takes the positions at the composition it waited for; a scroll step only moves the same window
fl = block_after(comment, 'void FollowLater() {')
check('ChatBubble::FollowSample == ChatBubble::FollowAt::Move' in comment and 'follow trace (' in comment, 'R39: the move takes the latest position known when it moves (04514f5); the chain is traced image by image')
pb = block_after(comment, 'void PlaceBubble(bool scrolling) {')
check('ChatBubble::ScrollFrame(' in pb and 'a scroll step changed the bubble' in pb, 'R39: a scroll step goes through ScrollFrame (a move only), any other change is logged')

# opening time (section.hero 150 to 167 ms): a click's bubble sends its conversation once, with its opening; diagnostics
sc = block_after(comment, 'void ShowConversation(')
oe = block_after(comment, 'bool OpenEarly(')
check('if (!forBubble) SwitchedShown(false);' in sc and ', nullptr, true);' in oe, 'opening time: the switch does not send the thread before the opening does (one sending)')
check('opening time: click -> host' in block_after(comment, 'void RevealBubble(const wchar_t* why) {') and 'UI thread held up' in comment, 'diagnostics: the opening by phase, the UI thread held up while a bubble is open')

# H2 6e60162: a « selecting » measured in another viewport is never shown on that rectangle — the page's first measure
check('StaleViewport(env.vw, env.vh' in block_after(comment, 'bool OpenEarly(') and 'if (bubble.holdForMeasure)' in block_after(comment, 'void RevealBubble(const wchar_t* why) {') and 'released' in block_after(comment, 'void OnAnchor('), 'selecting: a stale rectangle waits for the first anchor')

# monkey 0.3.561: a click over another element's open bubble opens at the click
check('EarlyOpenAllowed(bubble.open, bubble.selector, env.selector)' in block_after(comment, 'bool OpenEarly('), 'monkey: a click over another element open bubble opens at the click')

# monkey 0.3.561 #102 / #261: the bubbles' own elements left in `aiming` never block a click (only the panel's chips count)
tcs = block_after(comment, 'bool TakeCommentSelection(')
check('aiming.size() >= 10' not in tcs and 'BubbleClickAllowed(chips)' in tcs and 'anchorFor.empty()' in tcs, 'monkey #102 / #261: a click is refused only by 10 chips of the panel, and says so')
# monkey 0.3.561 F6: an element's conversation only in memory (its message waits in the queue) is found again by a click on it
fa = block_after(comment, 'const ChatStore::Header* FindAnchored(')
check('parked' in fa and 'SentConvo(' in fa and 'FindAnchored(page, env.selector' in block_after(comment, 'bool OpenEarly(') and 'FindAnchored(page, sel' in block_after(comment, 'bool TakeCommentSelection('), 'F6: a click finds its element conversation in memory too (not saved yet)')

# monkey 0.3.611 #116: a click in Commentaire refused by CommentFirst says why
check('a click in Commentaire is not the chat' in block_after(ui, 'bool CommentFirst('), 'monkey #116: a refused click in Commentaire is logged')

# #129: the model menu of the bubble is the panel's (same page, same host): a provider, a model or an effort chosen in the
# bubble goes to the conversation it shows — no layout makes the host ignore it, and a provider chosen is kept on the
# conversation's header (its next message, this comment, goes to that agent)
for t in ('setAgent', 'setModel', 'setEffort'):
    h = block_after(ui, 'if (type == "' + t + '")') or ui[ui.find('if (type == "' + t + '")'):][:200]
    check(h and not any(x in h for x in ('CommentLayout()', 'BubbleOpen()', 'bubble.open')), '#129: « ' + t + ' » from the bubble is handled as from the panel (no layout check)', h[:160])
sw = block_after(ui, 'void SwitchAgent(')
check('convo.data.head.agentId = id;' in sw and 'pendingModel = model;' in sw, '#129: a provider chosen is kept on the conversation (its header) for its next message')

# #130 (wins over R29), variant C: a new size that takes the bubble closer than 8 px to the bottom of the page area (its
# element on screen) slides it up along its element (SlideUp, 8 px from the page area's edges) — never at a scroll step
# (R39), never for a size chosen with the handle (R40); the page area's edges are 8 px (height at most page area - 16)
place = block_after(comment, 'void PlaceBubble(bool scrolling) {')
at = place.find('ChatBubble::PastBottom(')
grew = place[at:]
check(0 <= at < place.find('if (!bubble.anchor.set) {') and all(x in place[max(0, at - 300):at] for x in ('!scrolling', '!bubble.resized'))
      and 'ChatBubble::SlideUp(where, area, margin)' in grew[:900] and 'bubble.anchor.dy -= up;' in grew[:900] and 'PastBottom(where, er, area, margin)' in grew[:120],
      '#130 C: PlaceBubble slides a bubble grown past the bottom up along its element before choosing its anchor — not while scrolling, not resized by hand')
check('BubbleMarginCss = 8' in consts and '2 * BubbleMarginCss' in block_after(comment, 'void PlaceBubble(bool scrolling) {'),
      '#130 C: 8 px from the page area\'s edges; the height offered is at most the page area - 16 px')

ok = all(checks)
print(f'{sum(checks)} / {len(checks)} host contracts')
sys.exit(0 if ok else 1)
