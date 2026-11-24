#!/usr/bin/env python3
"""#128 host contracts, read from the source (comments removed, as tests/chat/host_contract_test.py does): another
provider's model opens a NEW chat when the chat has messages (the page's hint « Passer à Codex ouvre un nouveau chat »);
the same provider's model keeps the chat; an element's conversation (Mode Commentaire) starts again on the same element.

    python -I tests/chat/model_switch_128_test.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'src'
sys.stdout.reconfigure(encoding='utf-8')


def code(name):
    """The file without comments (// to the end of the line, /* */), strings kept (as host_contract_test.py)."""
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

checks = []


def check(ok, what, detail=''):
    checks.append(bool(ok))
    print(('PASS ' if ok else 'FAIL ') + what + (' — ' + str(detail) if detail and not ok else ''))


print('--- #128 host contracts')
ui, comment = code('chat-host-ui.h'), code('chat-comment.h')
sw = block_after(ui, 'void SwitchAgent(')
go = sw[sw.find('auto go'):]
other = block_after(go, 'if (id != agentId)')
check(sw and 'const bool newChat = !convo.data.items.empty();' in other, '#128: another agent — a new chat only when the chat has messages')
check(re.search(r'if \(newChat\) NewChatForAgent\(\);\s*else \{ convo\.data\.head\.agentId = id;', other), '#128: with messages a new chat; empty, the same (empty) chat takes the new agent')
check(other.find('agentId = id;') < other.find('NewChatForAgent()'), '#128: the agent is set before the new chat (its header takes the new agent)')
check('ConversationRecap(' not in sw and 'convo.recap.clear();' in other, '#128: no recap of the old chat any more (it stays in the history); an empty chat carries none')
check('pendingModel = model;' in go and 'MarkSwitch(id, model);' in go, '#128: the model chosen goes to the new session; « Passé à … » only where there are messages (MarkSwitch)')
check(re.search(r'if \(turn\.active\) \{ auto prev = std::move\(afterTurn\); afterTurn = \[prev, go\]', sw) and 'StopTurn' not in sw, '#128: a turn running is never stopped: the new chat waits for its end')

nc = block_after(comment, 'void NewChatForAgent(')
check(nc and re.search(r'if \(!was\.Anchored\(\)\) \{ NewConversation\(\); return; \}', nc), '#128: the panel\'s chat — a new conversation, as « Nouveau chat »')
check('NewAnchoredConversation(was.anchorSelector, was.anchorLabel, was.anchorPage, was.anchorSource, was.anchorKey)' in nc, '#128: an element\'s chat — a new conversation on the same element (selector, label, page, source, key)')
check('anchorElements[fresh] = it->second;' in nc and 'bubble.conv = fresh;' in nc, '#128: its element (image, source) goes with it; the open bubble shows it')
check('const ChatStore::Header was = convo.data.head;' in nc, '#128: the old header is copied before the new conversation replaces it')

sm = block_after(ui, 'if (type == "setModel")')
check('if (!agent.empty() && agent != agentId) { SwitchAgent(agent, id); return; }' in sm and 'NewConversation' not in sm and 'NewChatForAgent' not in sm,
      '#128: setModel — another agent goes through SwitchAgent; the same agent keeps the chat (no new conversation)')
check(re.search(r'if \(type == "setAgent"\) \{ SwitchAgent\(m\.Id\("id"\), ""\); return; \}', ui), '#128: setAgent (a provider whose models are not known yet) goes through SwitchAgent too')

marks = block_after(comment, 'void SyncMarks() {')
check('std::set<std::string> marked;' in marks and re.search(r'\(bubble\.open && h\.anchorSelector == bubble\.selector\) \|\| !marked\.insert\(h\.anchorSelector\)\.second\) \{ found0\.insert\(h\.id\); continue; \}', marks),
      '#128: an element with two conversations shows one mark (its newest), none while its bubble is open — and neither counts as gone')
fa = block_after(comment, 'const ChatStore::Header* FindAnchored(')
check(fa.find('for (const auto& h : anchored) if (match(h)) return &h;') >= 0, '#128: a click on the element opens its newest conversation (`anchored`: newest first)')

ok = all(checks)
print(f'{sum(checks)} / {len(checks)} #128 host contracts')
sys.exit(0 if ok else 1)
