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

turn = code('chat-host-turn.h'); comment = code('chat-comment.h')

# C2: a conversation shown « aside » while nothing runs becomes the engine's before anything is sent from it, and at the
# end of a turn when nothing waits for another conversation
soq = block_after(ui, 'void SendOrQueue(')
check(0 <= soq.find('TakeShownAside()') < soq.find('StartTurn('), 'C2: SendOrQueue takes the conversation shown aside before starting a turn')
check('TakeShownAside()' in block_after(turn, 'void ProcessQueue() {'), 'C2: the end of a turn (ProcessQueue, nothing queued) gives the engine to the conversation shown')
check('SwitchEngineTo(' in block_after(comment, 'bool TakeShownAside()'), 'C2: by the same path as a queued message (SwitchEngineTo)')

# C3: an element not found never closes the bubble while its conversation works or waits (a turn, an authorization, a
# queued message); it stays at its last place and follows the element again if it comes back.
on_anchor = block_after(comment, 'void OnAnchor(')
gone = block_after(on_anchor, 'if (e.gone)')
check('BubbleBusy()' in gone and gone.find('BubbleBusy()') < gone.find('CloseBubble('), 'C3: « gone » closes only when nothing works or waits for this conversation', gone[:160])
check('bubble.lost' in on_anchor, 'C3: an element found again is followed again (bubble.lost)')
busy = block_after(comment, 'bool BubbleBusy()')
check(all(x in busy for x in ('turn.active', 'waitingPermissions', 'HasQueued(')), 'C3: busy = a turn of its conversation, an authorization waiting, or a message queued for it')

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
start_turn = block_after(turn, 'void StartTurn(const std::string& text, const std::vector<std::string>& elementIds, const std::vector<std::string>& attachmentIds) {')
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
check('OfferedRoomCss(' not in block_after(comment, 'void PlaceBubble(bool scrolling) {'), '#130 C rule 4 (replaces R29): the height offered is not the room under the fixed edge — it is the page area - 16 px (checked below)')

# #130 C rule 7 (Paper « C règles (validées) », the owner, 24 Nov 2026): the browser NEVER scrolls the user's page to show a
# card — the old R47 fix (RevealCard: window.scrollBy from the host) is removed, everywhere
host_all = ''.join(code(f) for f in ('chat-comment.h', 'chat-host.h', 'chat-host-ui.h', 'chat-host-turn.h', 'chat-bubble.h'))
check('RevealCard' not in host_all and 'RevealScrollCss' not in host_all and 'scrollBy' not in comment and 'revealDue' not in comment,
      '#130 C rule 7: no automatic scroll of the site page for a card (RevealCard and its scrollBy are gone)')
# #130 C rules 9 and 10: no free side — below, else above, never over the element (its height limited to its side, or it switches side)
pb = block_after(comment, 'void PlaceBubble(bool scrolling) {')
check('ChatBubble::FlipSide(' in pb and pb.find('ChatBubble::FlipSide(') < pb.find('if (!bubble.anchor.set) {') and 'BubbleMinRoomCss' in pb,
      '#130 C rule 10: PlaceBubble lets a bubble below / above its element switch side before it is placed again')
check('p.side == ChatBubble::Side::Below' in pb and 'area.bottom - margin - p.rect.top' in pb,
      '#130 C rule 10: below its element the height offered stops 8 px from the page area\'s bottom (its thread scrolls)')

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

# real window (0.3.539): « the user is scrolling » = the page's scroll offset changed (not the page reflowing)

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
# O3 (« clic sans bulle », monkey 0.3.611 #116: the old note opened instead of a bubble): in Commentaire nothing declines a click
# on an element — the panel's limit of 10 chips is not this layout's; the early opening's ways out say why in the log
check('return false' not in tcs and 'BubbleClickAllowed' not in tcs, 'O3: TakeCommentSelection never declines a click (the note panel never opens instead of a bubble)')
oe = block_after(comment, 'bool OpenEarly(')
check(oe.count('Diag(') >= 4 and 'has no selector' in oe and 'right after the bubble closed' in oe, 'O3: the early opening says why it leaves a click to the full selection')
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

# ---------------------------------------------------------------- lot R (2026-11-24): the old annotation system is gone
def line_of(text, marker):
    at = text.find(marker)
    if at < 0:
        return ''
    end = text.find('\n', at)
    return text[at:end if end >= 0 else len(text)]


host_all = '\n'.join(code(n) for n in ('chat-host.h', 'chat-host-ui.h', 'chat-host-turn.h', 'chat-comment.h', 'chat-bubble.h', 'annotation-mode.h', 'annotation-ui.h'))
page_all = '\n'.join((ROOT / 'assets' / 'chat-panel' / n).read_text(encoding='utf-8') for n in ('panel.js', 'panel.css', 'dev-host.js'))
gone_host = ('PoseBatch', 'StartBatchTurn', 'BatchItem', 'composerBatch', 'activeBatch', 'annotations.batch', 'annotations.status', 'pendingAnnotations', 'chat-batch.h', 'chat-host-batch.h',
             'KeepAnnotation', 'annotation.kept', 'AskFromNote', 'NotePopover', 'note.Show', 'AUI::Launcher', 'launcher.', 'LauncherSync', 'LoadHistory', 'OpenHistory', 'CommentHistory',
             'OpenFromHistory', 'AnnotateHistoryCommand', 'AnnotateMarkdownItem', 'OnSubmit', 'ShowCardFor', 'RefreshStatus')
left = [w for w in gone_host if w in host_all]
check(not left, 'lot R (O7 and the removal): the host names no batch, no « garder en annotation », no note panel, no launcher, no card, no annotation history', left)
gone_page = ('annotations.batch', 'annotations.status', 'annotation.show', 'annotations.request', 'pendingAnnotations', 'renderBatch', 'batch-row', 'ann-card', 'annotation.kept', 'Garder en annotation',
             'Gardé en annotation', 'post("annotate"', 'Traiter ')
left = [w for w in gone_page if w in page_all]
check(not left, 'lot R: the page of the chat (panel.js, panel.css, dev-host.js) names no batch and no « garder en annotation »', left)
check(all(not (SRC / n).exists() for n in ('chat-batch.h', 'chat-host-batch.h', 'annotation-launcher.h', 'annotation-history.h', 'annotation-note.h', 'annotation-problem.h')),
      'lot R: the files of the batch, the launcher, the history and the note are deleted')
mode = code('annotation-mode.h')
on_select = block_after(mode, 'void OnSelect(const std::shared_ptr<Tab>& t, AH::Selection sel, double lag) {')
check('Chat::TakeSelection(' in on_select and 'Resume(taken && !commentFirst);' in on_select and 'note' not in on_select.replace('annotation', '').lower().replace('notes', ''),
      'lot R: a selection goes to the chat or nowhere (the picker resumes): no other surface opens')
main_cpp = (SRC / 'main.cpp').read_text(encoding='utf-8')
check("key=='H'&&Annotation" not in main_cpp and 'AnnotateHistoryCommand' not in main_cpp and 'key_ctrl_shift_h' not in (SRC / 'i18n-catalog.inc').read_text(encoding='utf-8'),
      'lot R: Ctrl+Maj+H is no longer a shortcut of the browser (nor a text of the catalog)')
# decision A (O12): the chat on a local file and in a private window
ask = line_of(ui, 'bool AskAvailable(const std::shared_ptr<Tab>& t) {')
check('ChatLocal::SiteOf(' in ask and '.file' not in ask and '!privateWindow || !ChatRoot().empty()' in ask, 'decision A: the chat is available on a local file, and in a private window once its own folder exists', ask)
check('privateWindow ? Ann::PrivateData() : Ann::DataRoot()' in line_of(host, 'std::wstring ChatRoot() {') and 'ChatFilesRoot()' in line_of(host, 'ChatStore::History& History() {')
      and 'ChatFilesRoot()' in line_of(host, 'std::wstring ProjectsFile() {') and 'ChatFilesRoot()' in line_of(turn, 'std::shared_ptr<CS::Store> Snapshots() {'),
      'decision A: in a private window the conversations, the site memory and the snapshots live in that window\'s own folder (erased with its ledger)')
wa = block_after(turn, 'void WriteAnnotations(std::vector<Element>& elements, const std::string& note) {')
check(0 <= wa.find('privateWindow && !Ann::RecordPrivate(a)') < wa.find('Ann::Store().WriteAnnotation('), 'decision A: in a private window an element is listed in the ledger before it is stored (no line, nothing stored)')
mss = block_after(turn, 'void StartSession() {')
check(0 <= mss.find('if (!site.file) {') < mss.find('"--site", site.origin'), 'decision A (security): a local file\'s session never gets the annotation bridge (it could not be restricted to a site)')
df = block_after(ui, 'void DetectFolder() {')
check(all(x in df for x in ('if (site.file) {', 'ChatFolder::detail::Refused(dir)', 'ChatFolder::detail::TooWide(dir)', 'FolderDecided(suggestion.sure, suggestion.doubt)')) and 'Parent(' not in df[:df.find('ChatFolder::Detect(')],
      'decision A (security): a local file\'s project folder is its own folder, never one above; a wide or refused folder is asked, never used silently')
check('SiteOf(' in line_of(comment, 'std::string ModeOrigin() {') and 'ChatLocal::Classify(' not in ui.replace('ChatLocal::Classify(U8(t->uri)).label', ''),
      'decision A: the host reads a page\'s site through ChatLocal::SiteOf everywhere (a file\'s folder)')
# the chat opens with the mode in « Panneau »
me = line_of(ui, 'void ModeEntered() {')
oas = block_after(ui, 'void OpenAtModeStart() {')
check('else OpenAtModeStart();' in me and 'CommentLayout()' in oas and 'quietOpening = true; OpenPanel();' in oas and 'openedByMode = true' in oas,
      'lot R: in « Panneau » the panel opens with the mode (never in « Commentaire »), quietly')
op = block_after(ui, 'void OpenPanel() {')
check('if (quietOpening) quietOpening = false;' in op and 'if (quietOpening && !BubbleOpen()) quietOpening = false;' in block_after(ui, 'if (type == "ready")'),
      'lot R: opened by the mode, the panel takes no keyboard focus (neither at once nor when its page is ready)')
act = block_after(mode, 'void Activate(const Start& st) {')
# « Ton agent »: « Connecter un agent » no longer opens by itself (the tutorial the first time, the band's reminder else):
# the tutorial's rule is the one left to read « no chat asked ».
check('first.chatOpen = Chat::AskedFor();' in act and 'return Open() && !openedByMode;' in line_of(ui, 'bool AskedFor() {'),
      'lot R: the launch rules (tutorial, « Connecter un agent ») still see « no chat asked » when the mode opened the panel itself')
# « 10 éléments au maximum » (validated 2026-11-24): the strip, 4 s, and no frame nor shot for the click
ts = block_after(ui, 'bool TakeSelection(const std::shared_ptr<Tab>& t, std::uint64_t serial, bool captureImpossible) {')
full = block_after(ui, 'void SayComposerFull() {')
check('>= ComposerMax) { if (!Open()) OpenPanel(); SayComposerFull(); return false; }' in ts and 'ComposerMax = 10' in ui and 'LimitStripMs = 4000' in ui
      and 'Strip("limit", "neutral", TR(chat_strip_limit))' in full and 'SetTimer(post, LimitTimer, LimitStripMs' in full and 'ClearStrip("limit")' in line_of(ui, 'if (w == LimitTimer)'),
      'lot R: an 11th element in « Panneau » is refused with the neutral strip, for 4 s, shown again at each extra click')
cat = (SRC / 'i18n-catalog.inc').read_text(encoding='utf-8')
check('I18N(chat_strip_limit, L"10 éléments au maximum. Envoyez la demande ou retirez-en un.", L"10 elements max. Send your request or remove one.")' in cat, 'lot R: the limit sentence, FR and EN, as validated')
on_select = block_after(mode, 'void OnSelect(const std::shared_ptr<Tab>& t, AH::Selection sel, double lag) {')
at_full = on_select.find('Chat::ComposerFull()')
check(0 <= at_full < on_select.find('StartShot(t)') and 'Resume(false); return;' in on_select[at_full:at_full + 400] and at_full < on_select.find('shot = ShotJob{}'),
      'lot R: the click beyond 10 elements starts no native frame and no shot, and moves no focus')
tg = block_after(ui, 'void Toggle(bool keyboard) {')
check(0 <= tg.find('if (Open() && openedByMode) {') < tg.find('if (Open()) { ClosePanel(); return; }') and 'view.Focus()' in tg[:tg.find('if (Open()) { ClosePanel(); return; }')],
      'lot R: Ctrl+Maj+D on the panel opened with the mode gives it the keyboard; only the next one closes it')
sq = block_after(host, 'void SendQueue() {')
check('m.Set("more", Str(TRF(chat_queue_more, L"{n}")))' in sq and 'I18N(chat_queue_more, L"+{0} en attente", L"+{0} queued")' in cat,
      'lot R (O5): the queue message carries the « +N en attente » line in the interface language')
# Owner decision (24 Nov 2026): never signed in reads « Claude Code n'est pas connecté. »; « Vous n'êtes plus connecté à … » only when
# the host saw the agent signed in since Browser started (a detection said so, or a session was ready)
check('I18N(chat_agent_never_signed_in, L"{0} n\'est pas connecté.", L"{0} is not signed in.")' in cat
      and 'I18N(chat_agent_signed_out, L"Vous n\'êtes plus connecté à {0}.", L"You are no longer signed in to {0}.")' in cat,
      'signed out: the two sentences, FR and EN (never signed in / no longer signed in)')
check('if (info.status == "notConnected") a.Set("notice", Str(info.signedIn ? TRF(chat_agent_signed_out, W(std::string(def.name))) : TRF(chat_agent_never_signed_in, W(std::string(def.name)))));' in host,
      'signed out: the state carries the sentence in the interface language — « plus connecté » only when the agent was seen signed in')
# « Ton agent »: the chat reads the agents' one status (src/agent-status.h): « ready » is the detection that says signed in
ad = block_after(turn, 'void ApplyStatus(const std::string& id, const AgentStatus::Status& s) {')
check(0 <= ad.find('if (s.state == AgentStatus::State::Ready) a.signedIn = true;') < ad.find('if (sessionReady && id == agentId) return;')
      and turn.count('signedIn = true') == 2 and 'info.cli = info.prepared = true; info.signedIn = true;' in turn and 'bool signedIn = false;' in host,
      'signed out: « seen signed in » is set by a detection that says so and by a ready session, nowhere else; false at start')
# Lot 5's real trial with Codex (24 Nov 2026): « Demander » = Codex's « read-only »; it refused instead of asking, and the
# annotation was closed as resolved with 0 file changed
ct = block_after(turn, 'void CompleteTurn(const std::string& stopReason, const CS::TurnSummary* summary, const std::vector<std::pair<std::string, CS::FileDiff>>* diffs')
check('const bool worked = summary ? changed : !turn.agentFiles.empty();' in ct
      and 'const auto outcome = ChatOptions::AnnotationOutcome(stopReason, worked, CurrentModeKind() == "plan");' in ct
      and 'const bool resolved = outcome == ChatOptions::AnnotationEnd::Resolved;' in ct and 'CloseAnnotations(turn.elements, resolved,' in ct
      and 'TR(chat_release_unchanged)' in ct and 'CloseAnnotations(turn.elements, done' not in ct,
      'lot 5 (Codex): an annotation is resolved only when the turn changed a file; ended with nothing changed, it is given back with its reason')
ss = block_after(turn, 'void StartSession() {')
acp_cpp = (SRC / 'acp-client.cpp').read_text(encoding='utf-8'); acp_h = (SRC / 'acp-client.h').read_text(encoding='utf-8'); rt_cpp = (SRC / 'agent-runtime.cpp').read_text(encoding='utf-8')
check('o.instructions = ChatPrompt::Instructions();' in ss and 'if (caps.systemPrompt && !opts.instructions.empty()) {' in acp_cpp
      and 'prompt.Set("mode", SJ::Value::Str("append"));' in acp_cpp and 'c.systemPrompt = Object(Object(caps, "sessionCapabilities"), "systemPrompt") != nullptr;' in acp_cpp
      and 'std::string instructions;' in acp_h,
      'lot 5 (Codex): the session is given the approval rule as appended instructions, only when the agent advertises that it takes some')
check('m.ask = {"read-only"};' in rt_cpp and 'm.automatic = {"agent", "workspace-write"};' in rt_cpp and 'm.never = {"agent-full-access"};' in rt_cpp,
      'lot 5 (Codex): « Demander » stays Codex\'s « read-only » (the only mode where an edit waits for the user), « Auto » its « agent » / « workspace-write »; full access is never offered')
# Lot R, private window closed in the middle of a turn (found 25 Nov 2026, hidden-desktop run): the snapshot store kept its
# « turn.lock » open in the window's own folder, which could then not be removed — conversation and ledger stayed until the
# next start. The chat lets go of the store when it shuts down, and it shuts down BEFORE the private window's files are erased.
sd = block_after(ui, 'void Shutdown() {')
main_all = (SRC / 'main.cpp').read_text(encoding='utf-8')
check(0 <= sd.find('for (auto& s : disposed) s->WaitStopped(3000);') < sd.find('snapshots.reset();') < sd.find('view.Destroy();')
      and 0 <= main_all.find('case WM_DESTROY:ChatShutdown();') < main_all.find('AnnotationShutdown();', main_all.find('case WM_DESTROY:ChatShutdown();')),
      'lot R (private window): at the close the chat stops its agent, lets go of the snapshot store, then the window\'s notes and folder are erased')
cn = block_after(ui, 'void Connect(const std::string& id, const std::string& methodId) {')
check('if (!method && info.auth.empty() && info.status == "notConnected" && info.cli) { own = AgentRuntime::DefaultTerminalSignIn(def->rt); method = &own;' in cn
      and 'else if (id != agentId) DetectAgents();' in cn,
      'sign-in (O8): signed out with no method named yet, « Se connecter » runs the CLI\'s own sign-in; another agent\'s line is detected again once its console closes')

ok = all(checks)
# Lot 1b (real-window monkey, 24 Nov 2026, seed 1 #185 / #219, replayed without a window): the opening's acknowledgement is the
# page's answer to the OPENING layout — nothing sent before it makes the page publish a size for the layout of before
ob = block_after(comment, 'void OpenBubble('); pb = block_after(comment, 'void PlaceBubble(bool scrolling) {')
check('bubble.opening = panelReady; PlaceBubble(); bubble.opening = false;' in ob and pb.count('!bubble.opening) SendLayout(false)') == 2
      and pb.count('SendLayout(false)') == 2 and ob.index('bubble.opening = panelReady') < ob.index('SendLayout(true)'),
      'lot 1b: placing a bubble that opens sends no layout {open: false} before its opening layout (the page answered it with the height of the conversation of before)')
sa = block_after(comment, 'void ShowAside('); afb = block_after(comment, 'void AsideForBubbleShown('); sc = block_after(comment, 'void ShowConversation(')
check('if (forBubble) return;' in sa and sa.index('if (forBubble) return;') < sa.index('SendState()') and 'RefreshWaitStrip();' in afb
      and 'if (!(bubble.open && bubble.conv == id)) { SendState(); SendThread(); SendQueue(); }' in afb
      and sc.count('ShowAside(std::move(c), forBubble); if (then) then(); if (forBubble) AsideForBubbleShown(') == 2 and 'ShowAside(std::move(c));' not in comment,
      'lot 1b: a conversation shown aside for a bubble opening (a turn runs elsewhere) is sent once, with the opening layout — never first, in the layout of before')
oe2 = block_after(comment, 'bool OpenEarly(')
check('NewAnchoredConversation(env.selector, label, page, "", "", true)' in oe2 and oe2.index('AsideForBubbleShown(id)') > oe2.index('OpenBubble(id, &element, env.selector, false, label)')
      and 'NewAnchoredConversation(sel, label, page, "", key, true)' in comment and comment.count('AsideForBubbleShown(id);') == 3,
      'lot 1b: a new conversation for a clicked element is sent once too (it was first drawn with the draft of the element of before)')
# M5 (real-window monkey, 24 Nov 2026, seed 1 #81 / #101): a click while the chat panel is still on screen (sliding out) — its
# rectangle is of a page about to widen: the bubble waits for the page's first measure instead of being shown, then moved
oe5 = block_after(comment, 'bool OpenEarly(')
check('const bool areaMoving = Shown();' in oe5 and oe5.count('areaMoving') >= 4 and 'holdForMeasure = stale || areaMoving' in oe5,
      'M5: a click while the chat panel is still on screen — the bubble waits for the first measure of the page')

# Permission circuit (owner's trial 0.3.952, 24 Nov 2026): one answer per request; a refused step is kept as refused
respond = block_after(ui, 'if (type == "permission.respond")')
check(0 <= respond.find('waitingPermissions.find(request)') < respond.find('if (asked == waitingPermissions.end())') < respond.find('session->RespondPermission(request, option)')
      and respond.count('session->RespondPermission(') == 1 and respond.find('session->RespondPermission(request, option)') < respond.find('waitingPermissions.erase(request)'),
      'permission: one answer per request — a request that no longer waits (answered, withdrawn) is answered nothing more')
check(0 <= respond.find('RefusingOption(asked->second, option)') < respond.find('session->RespondPermission(request, option)') < respond.find('if (refused) MarkRefused(tool);'),
      'permission: a refusal (the option\'s own kind, read from the request as it was sent) marks the step it was for')
mark = block_after(turn, 'void MarkRefused(const std::string& toolId) {')
check('turn.refusedTools.insert(toolId)' in mark and 'convo.data.items[it->second].Set("refused", SJ::Value::Bool(true))' in mark and 'Send("toolCallUpdate"' in mark,
      'permission: a refused step is kept as refused in the conversation (opened again, it is not shown) and the page is told')
tool_call = block_after(turn, 'case T::ToolCall: {'); tool_update = block_after(turn, 'case T::ToolCallUpdate: {')
check('turn.refusedTools.count(e.tool.id)' in tool_call and tool_update.count('turn.refusedTools.count(e.tool.id)') >= 2
      and 'e.tool.status == "failed" && turn.droppedTools.count(e.tool.id)' in tool_update,
      'permission: the agent\'s later updates of a refused step keep it refused; a step whose request was withdrawn unanswered and that fails never happened')
check('turn.droppedTools.insert(tool)' in block_after(turn, 'case T::PermissionDismissed: {'), 'permission: a request withdrawn unanswered (turn stopped) is remembered for its step')
on_perm = block_after(turn, 'void OnPermission(const Acp::PermissionRequest& p) {')
check(on_perm.count('Send("permission.request"') == 1 and 'waitingPermissions[p.requestId] = m;' in on_perm and 'm.Set("toolCallId"' in on_perm,
      'permission: the agent\'s request is sent to the page once, with its tool call, and kept while it waits')

# Lot 1d (the owner's decisions of 24 Nov 2026 on iframes; Paper « Iframes · cas 1 … A », « Iframes · cas 3 »)
page_js = (ROOT / 'src' / 'annotation-script.h').read_text(encoding='utf-8')
fp = block_after(page_js, 'function followPost() {')
check('stick({ x: r.left + at.x, y: r.top + at.y, w: r.width, h: r.height, vw: innerWidth, vh: innerHeight }, at.edge)' in fp and 'function stick(o, e) {' in page_js
      and 'edge = cut(edge, x + r.left, y + r.top, r.width, r.height);' in page_js,
      'lot 1d, case 1 = A: the place given to the host stays in the iframe\'s box (its border box) — the bubble stuck to the edge the element left by')
check('follow.key ? keyFind(follow.key) : follow.deep ? deepFind(follow.deep)' in fp and '!el.isConnected || !follow.chain' in fp
      and 'm.el = m.key ? keyFind(m.key) : cmFind(m.selector)' in block_after(page_js, 'function cmPlace(m) {'),
      'lot 1d, case 3: the page script finds an iframe\'s element again after its iframe reloaded (the bubble\'s element and the marks)')
fb = block_after(comment, 'void FollowBubbleElement(bool current) {'); sm = block_after(comment, 'void SyncMarks() {')
check('key = FrameKeyOf(bubble.conv)' in fb and 'f.Set("key", Str(key))' in fb and 'ChatBubble::FrameKey(k) ? k : std::string()' in block_after(comment, 'std::string FrameKeyOf(const std::string& id) {'),
      'lot 1d, case 3: after a reload of the page the host gives an iframe\'s element back to the script by its key')
check('!ChatBubble::FrameKey(h.anchorKey) || !marked.insert(h.anchorKey).second' in sm and 'o.Set("key", Str(h.anchorKey))' in sm,
      'lot 1d, case 3: a conversation of an iframe\'s element has its mark too (by its key); a web component\'s still has none')

# Owner's trial 0.3.952, decision A: another application in front no longer hides the bubble (the click that brought the
# browser back fell on the page: another element selected, the request « asked twice »); away, it never takes the keyboard
away = block_after(comment, 'void BubbleAway(')
check('inline constexpr bool BubbleHidesBehindOthers = false;' in comment and 'if (BubbleHidesBehindOthers) SetWindowRgn(bubble.hwnd, CreateRectRgn(0, 0, 0, 0), TRUE)' in away and 'bubble.away = true;' in away,
      'A: another application in front — the bubble keeps its region (it stays on the browser\'s window), only its state says « away »')
ktry = block_after(comment, 'void KeyboardTry() {')
check('bubble.away' in ktry.split('SetForegroundWindow')[0] and '!bubble.away' in comment[comment.find('if (m == WM_APP + 2)'):comment.find('if (m == WM_APP + 3)')],
      'A: while another application is in front the bubble never takes the foreground nor the keyboard')
place = block_after(comment, 'void PlaceBubble(bool scrolling) {')
check('!BubbleHidden()' in place and 'bubble.away' not in place, 'A: the bubble on screen behind another application still follows its element (its region is kept up to date)')
check('if (BubbleHidden())' in block_after(comment, 'void RevealBubble(const wchar_t* why) {'), 'A: a bubble that becomes ready while another application is in front is shown (never left invisible)')

# Designer, 25 Nov 2026: Codex's « Additional sandbox permissions » is said in the browser's language, before any kind is looked at
ptitle = block_after(turn, 'std::wstring PermissionTitle(const Acp::ToolCall& t) {')
catalog = (ROOT / 'src' / 'i18n-catalog.inc').read_text(encoding='utf-8')
known_title = block_after(turn, 'std::wstring KnownStepTitle(const std::string& title) {'); tool_json = block_after(turn, 'SJ::Value ToolJson(const Acp::ToolCall& t, bool update) {')
check('ChatTools::SameTitle(title, "Additional sandbox permissions") ? TRF(chat_perm_sandbox, AgentShort(agentId)) : std::wstring()' in known_title
      and 'const std::wstring known = KnownStepTitle(t.title);' in tool_json and 'known.empty() ? Str(ClipUtf8(t.title.empty() ? t.kind : t.title, 400)) : Str(known)' in tool_json
      and '!known.empty() || t.kind.empty() || t.kind == "switch_mode" ? std::string("other") : t.kind' in tool_json,
      "permission: Codex's step « Additional sandbox permissions » reads in the thread as on the card (the same sentence, its kind's wording never replaces it); every other title stays as the agent sends it")
check(0 <= ptitle.find('KnownStepTitle(t.title); !known.empty()) return known;') < ptitle.find('t.kind == "edit"')
      and "I18N(chat_perm_sandbox, L\"{0} demande plus d'accès à cet ordinateur\", L\"{0} is asking for more access to this computer\")" in catalog,
      "permission: Codex asking for more access — « Codex demande plus d'accès à cet ordinateur » / « Codex is asking for more access to this computer »")

# C1 (journeys of 25 Nov 2026, « chat page delayed by 4 s », replayed on a hidden desktop): a bubble opened before its page
# exists is never shown empty — the 2 s safety waits for the page (at most BubblePageWaitMs), the page's « ready » re-arms
# the short safety and its acknowledgement shows the surface with the conversation.
b2 = comment[comment.find('if (m == WM_TIMER && w == 0xB2)'):]; b2 = b2[:b2.find('RevealBubble(L"without acknowledgement (safety delay)")')]
check('if (!panelReady && bubble.open && bubble.waitingAck && Ann::NowMs() - bubble.openedAt < BubblePageWaitMs)' in b2 and 'SetTimer(h, 0xB2, 250, nullptr); return 0;' in b2,
      'C1: a bubble whose page is still being created is not revealed by the safety delay (it waits for its page, at most BubblePageWaitMs)')
check('SetTimer(bubble.hwnd, 0xB2, 300, nullptr)' in block_after(comment, 'void BubblePageReady() {')
      and 0 <= ready.find('BubblePageReady();') < ready.find('SendThread();'),
      'C1: the page ready, a bubble that waited for it is shown with the acknowledgement of its opening layout (the thread sent with it)')
# « Ton agent », 2nd defect of 25 Nov (Fix's « absent » check): an agent installed / signed in from the chat's block turns
# ready — the keyboard goes back to the input where the text waits (the browser in front only).
tas = (SRC / 'chat-ton-agent.h').read_text(encoding='utf-8'); csr = block_after(tas, 'void ChatSetupReady(AgentStatus::Agent a) {')
kbn = block_after(tas, 'void KeysBackNow(AgentStatus::Agent a) {'); kbt = block_after(tas, 'void KeysBackTick() {')
check('if (started && act != "cancel") setupFromChat.insert(a);' in tas and 'setupFromChat.erase(a)' in csr and 'if (BrowserInFront()) { KeysBackNow(a); return; }' in csr
      and 'Send("focus", std::move(f)); view.Focus();' in kbn and 'KeyboardToBubble(' in kbn and 'PanelKeyboardBack();' in kbn
      and 'if (s.Ready() && before != AgentStatus::State::Ready) ChatSetupReady(s.agent);' in turn,
      '« Ton agent »: an agent set up from the chat\'s block turns ready — the keyboard back to the input (panel or bubble), the browser in front')
check('SetTimer(post, KeysBackTimer, 100, nullptr)' in csr and 'if (!BrowserInFront()) return;' in kbt and 'if (f && f != ::window)' in kbt and 'GetTickCount64() > keysBack.until' in kbt
      and 'if (w == KeysBackTimer) { void KeysBackTick(); KeysBackTick(); return 0; }' in ui,
      '« Ton agent »: ready while its sign-in console holds the foreground — the keys go back once the browser is in front again (never over the person\'s own focus, bounded in time)')


print(f'{sum(checks)} / {len(checks)} host contracts')
sys.exit(0 if ok else 1)
