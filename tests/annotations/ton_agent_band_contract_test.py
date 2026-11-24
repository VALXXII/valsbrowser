"""« Ton agent » (worker C): the band's reminder and the « Connecter un agent » window, as source contracts.

Headless, no build: the rules live in src/annotation-mode.h (included by main.cpp, it cannot run alone), so they are
checked in the source, like tests/chat/host_contract_test.py:
  1. the MCP « Connecter un agent » window never opens by itself: no launch variant is ever asked
     (OpenConnect(true …)), Ann::Activate opens nothing, and every remaining way in is an explicit click
     (Réglages › Agents › « Connecter un agent externe », the tutorial's « Autre app ou réglage manuel »);
  2. the band's reminder is the CHAT's: every SetReminder(...) passes false (a window takes the place) or
     ReminderWanted(), which reads the agents' one status (AnnotationSettings::BandReminder over AgentStatus), never
     the MCP agents; it follows every status change (AgentStatus::Subscribe) and the chat panel (Ann::SyncReminder);
  3. clicking it opens « Ton agent » (OpenTonAgent(TonAgentFrom::Band), src/ton-agent-entry.h; worker B defines it,
     a stand-in until then);
  4. Réglages › Agents › « Connecter un agent externe » asks for that window (main.cpp), and the settings' install
     takes its consent from the row's click only (ChatAgentAct).
The behaviour behind the rule (which statuses show the reminder) is tests/agents_settings_test.cpp.
Usage: python tests/annotations/ton_agent_band_contract_test.py
"""
from __future__ import annotations
import pathlib, re, sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')   # « » → on any console

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'src'
failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def read(name: str) -> str:
    return (SRC / name).read_text(encoding='utf-8')


def body(text: str, signature: str) -> str:
    """The braces-balanced body of the function whose definition starts with `signature`."""
    at = text.find(signature)
    if at < 0:
        return ''
    i = text.index('{', at)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return ''


mode = read('annotation-mode.h')
ui = read('chat-host-ui.h')
main = read('main.cpp')
entry = read('ton-agent-entry.h')
panel = read('settings-panel.h')

# ---- 1. « Connecter un agent » never opens by itself --------------------------------------------------------------
check(not re.search(r'\bOpenConnect\(\s*true', mode), 'no « Connecter un agent » launch variant is ever asked (OpenConnect(true …))')
activate = body(mode, 'void Activate(const Start& st) {')
check(bool(activate), 'Ann::Activate found')
check('OpenConnect(' not in activate and 'connect.Show(' not in activate, 'mode start (Ann::Activate) opens no « Connecter un agent » window')
check('Prefs::Prompt(' not in activate and 'Prefs::Declines(' not in activate, 'mode start no longer reads « Proposer de connecter un agent » / « Plus tard »')
check('banner.SetReminder(ReminderWanted())' in activate, 'mode start: the reminder is the chat\'s (ReminderWanted())')
allowed_open = [
    re.compile(r'void OpenConnect\(bool launch, bool manual = false\) \{'),                                   # its definition
    re.compile(r'if \(manual && s\.active\) \{ OpenConnect\(false, true\); return; \}'),                     # tutorial « Autre app ou réglage manuel » (a click)
    re.compile(r'lh\.connect = \[\] \{ launcher\.HideCard\(\); card = Card::None; OpenConnect\(false\); \};'), # launcher card « Connecter » (a click; gone with lot R)
    re.compile(r'void AnnotationOpenConnect\(bool manual\) \{ if \(!halted\) Ann::OpenConnect\(false, manual\); \}'),  # the entry point (settings)
]
stray = [line.strip() for line in mode.splitlines() if re.search(r'\bOpenConnect\(', line) and 'AnnotationOpenConnect(bool)' not in line
         and not any(p.search(line) for p in allowed_open)]
check(not stray, 'every other way into « Connecter un agent » is an explicit click' + (f' — stray: {stray}' if stray else ''))
callers = []
for path in list(SRC.glob('*.h')) + list(SRC.glob('*.cpp')):
    if path.name in ('annotation-mode.h', 'annotation-entry.h'):
        continue
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        if 'AnnotationOpenConnect(' in line:
            callers.append((path.name, line.strip()))
external = [c for c in callers if c[0] == 'main.cpp' and 'hooks.openExternalAgents=[]{settingsDrawer.Hide(false,true);AnnotationOpenConnect(false);};' in c[1]]
check(len(external) == 1, 'Réglages › Agents › « Connecter un agent externe » asks for the window (main.cpp, not on its manual fallback)')
others = [c for c in callers if c not in external]
check(not others, 'nothing else calls AnnotationOpenConnect' + (f' — {others}' if others else ''))

# ---- 2. The reminder is the chat's ----------------------------------------------------------------------------------
reminder = body(mode, 'bool ReminderWanted() {')
check('AnnotationSettings::BandReminder(' in reminder and 'Chat::Open()' in reminder, 'ReminderWanted(): BandReminder over the chat agents, the chat panel closed')
now = body(mode, 'AnnotationSettings::ChatAgents ChatAgentsNow() {')
check('AgentStatus::Get(AgentStatus::Agent::Claude)' in now and 'AgentStatus::Get(AgentStatus::Agent::Codex)' in now, 'the chat agents come from the one status (AgentStatus::Get)')
check('agents.any()' not in reminder and 'agents.any()' not in now, 'the reminder no longer reads the MCP agents (agents.any())')
calls = []
for name, text in (('annotation-mode.h', mode), ('chat-host-ui.h', ui), ('chat-host.h', read('chat-host.h')), ('chat-host-turn.h', read('chat-host-turn.h'))):
    for m in re.finditer(r'banner\.SetReminder\(([^;]*?)\);', text):
        calls.append((name, m.group(1).strip()))
bad = [c for c in calls if not (c[1].startswith('false') or c[1] == 'ReminderWanted()')]
check(bool(calls) and not bad, 'every SetReminder(...) is false (a window in its place) or ReminderWanted()' + (f' — {bad}' if bad else ''))
check(bool(re.search(r'AgentStatus::Subscribe\(\[\]\(const AgentStatus::Status&\) \{ SyncReminder\(\); \}\);', mode)), 'the reminder follows every status change (AgentStatus::Subscribe → SyncReminder)')
sync = body(ui, 'void SyncChromeBits() {')
check('Ann::SyncReminder();' in sync and 'agents.any()' not in sync, 'the chat panel opening / closing re-applies the rule (Ann::SyncReminder)')

# ---- 3. Its click opens « Ton agent » --------------------------------------------------------------------------------
check(bool(re.search(r'bh\.connect = \[\] \{ OpenTonAgent\(TonAgentFrom::Band\); \};', mode)), 'the reminder\'s click opens « Ton agent » (OpenTonAgent(TonAgentFrom::Band))')
check('void OpenTonAgent(TonAgentFrom from);' in entry and 'enum class TonAgentFrom { Band, Chat, Settings };' in entry, 'src/ton-agent-entry.h: the shared entry point for worker B')
check(bool(re.search(r'#ifndef BROWSER_TON_AGENT_SCREEN\s*\n(//[^\n]*\n)*void OpenTonAgent\(TonAgentFrom from\) \{', mode.replace('\r\n', '\n'))), 'a stand-in until B defines it (BROWSER_TON_AGENT_SCREEN)')

# ---- 4. Settings: the click is the consent, the switch is gone -----------------------------------------------------
act = body(mode, 'void ChatAgentAct(AnnotationSettings::ChatApp app, AnnotationSettings::ChatAction action) {')
check('AgentStatus::Install(AgentStatus::UserClicked(agent))' in act and 'AgentStatus::Retry(AgentStatus::UserClicked(agent))' in act, 'Réglages › Agents: Install / Retry take their consent in ChatAgentAct (from the row\'s click)')
users = [m.start() for m in re.finditer(r'AgentStatus::UserClicked\(', mode)]
check(len(users) == 2 and all(act and mode.find(act) <= u <= mode.find(act) + len(act) for u in users), 'annotation-mode.h makes no consent anywhere else')
activate_panel = body(panel, 'void Activate(int hit, bool keyboard) {')
check('ChatAgentAction(' in activate_panel and 'hooks_.openExternalAgents()' in activate_panel, 'the drawer acts only from Activate (a click, Entrée, Espace, UIA Invoke)')
check('AnnotationPrompt' not in panel and 'settings_connect_prompt' not in panel, 'Réglages: no « Proposer de connecter un agent » switch')

print(f'\n{checks} checks, {len(failures)} failures')
sys.exit(1 if failures else 0)
