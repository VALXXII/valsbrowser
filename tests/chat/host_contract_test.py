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
check('.mode = id' in set_mode, 'M9: a mode chosen is remembered for the site')
check('ChoiceForNewSession(' in block_after(turn, 'std::string WantedMode('), 'M9: the mode shown before the session (R28) is the one it will get')

# E3: a bubble shown aside (another element's turn runs) never changes the running session; its choice waits for its own
for t in ('setModel', 'setEffort', 'setMode'):
    h = block_after(ui, 'if (type == "' + t + '")')
    check('aside.on' in h and h.find('aside.on') < h.find('session->') if 'session->' in h else 'aside.on' in h,
          'E3: « ' + t + ' » from a bubble aside keeps the choice for that conversation, the running session untouched')
check('aside.on' in block_after(ui, 'void MarkSwitch('), 'E3: the « Passé à … » mark goes into the conversation shown aside')

ok = all(checks)
print(f'{sum(checks)} / {len(checks)} host contracts')
sys.exit(0 if ok else 1)
