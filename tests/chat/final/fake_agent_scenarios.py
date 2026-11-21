#!/usr/bin/env python3
"""The fake agent's realistic scenarios (tests/acp/fake_agent.cpp, « vrai-* », --start-mode, --new-delay), driven over its
stdin / stdout exactly as the browser does (ACP v1, JSON-RPC, one message per line). No window, no network, no real agent.

    python -I tests/chat/final/fake_agent_scenarios.py [--exe <fake-acp-agent.exe>]   # builds it into build/chat-qa/final/tools when not given

Checks: the session starts in « auto » when asked; each scenario's updates (announcements, tool calls, permission, error
529, slowness, cancel) and that the answer is in French; the streamed answer, once joined, is exactly the text (no chunk
ever cuts a UTF-8 character); the old scenarios still answer as before (reply, annonce, permission).
"""
from __future__ import annotations
import json
import pathlib
import queue
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = ROOT / 'build' / 'chat-qa' / 'final' / 'tools'
GXX = ROOT / 'tools' / 'w64devkit' / 'bin' / 'g++.exe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-Wno-missing-field-initializers', '-static', '-municode', '-Isrc']
results = []


def check(name, ok, detail=''):
    results.append((name, bool(ok)))
    print(('PASS ' if ok else 'FAIL ') + name + (f' — {detail}' if detail and not ok else ''), flush=True)
    return ok


def build() -> pathlib.Path:
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / 'fake-acp-agent.exe'
    r = subprocess.run([str(GXX), *FLAGS, 'tests/acp/fake_agent.cpp', '-o', str(exe)], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        raise SystemExit('fake agent does not compile')
    return exe


class Agent:
    def __init__(self, exe, *args, cwd=None):
        self.p = subprocess.Popen([str(exe), *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=cwd)
        self.q = queue.Queue(); self.next = 1
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.p.stdout:
            try:
                self.q.put(json.loads(line.decode('utf-8')))
            except ValueError:
                pass

    def send(self, msg):
        self.p.stdin.write((json.dumps(dict(msg, jsonrpc='2.0'), ensure_ascii=False) + '\n').encode('utf-8')); self.p.stdin.flush()

    def call(self, method, params, on_request=None, timeout=20.0):
        """Sends a request; collects every notification until its answer. `on_request(msg)` answers the agent's own requests."""
        rid = self.next; self.next += 1
        self.send({'id': rid, 'method': method, 'params': params})
        seen = []; end = time.time() + timeout
        while time.time() < end:
            try:
                m = self.q.get(timeout=0.2)
            except queue.Empty:
                continue
            if m.get('id') == rid and 'method' not in m:
                return m, seen
            seen.append(m)
            if 'method' in m and 'id' in m and on_request:
                answer = on_request(m)
                if answer is not None:
                    self.send({'id': m['id'], 'result': answer})
        return None, seen

    def close(self):
        try:
            self.p.stdin.close(); self.p.wait(5)
        except Exception:
            self.p.kill()


def text_of(seen):
    return ''.join(m['params']['update']['content']['text'] for m in seen
                   if m.get('method') == 'session/update' and m['params']['update'].get('sessionUpdate') == 'agent_message_chunk')


def kinds(seen):
    out = []
    for m in seen:
        if m.get('method') == 'session/update':
            out.append(m['params']['update'].get('sessionUpdate'))
        elif 'method' in m:
            out.append(m['method'])
    return out


def start(exe, *args):
    a = Agent(exe, '--claude-like', *args)
    r, _ = a.call('initialize', {'protocolVersion': 1, 'clientCapabilities': {}})
    s, _ = a.call('session/new', {'cwd': str(ROOT), 'mcpServers': []})
    return a, r, s


def prompt(a, sid, text, on_request=None, timeout=25.0):
    return a.call('session/prompt', {'sessionId': sid, 'prompt': [{'type': 'text', 'text': 'contexte : plan, tool, mode'}, {'type': 'text', 'text': text}]},
                  on_request, timeout)


FRENCH = ("C'est fait", 'Je ', "D'accord", 'Voici', 'Voilà', 'Trois blocs')


def main():
    exe = pathlib.Path(sys.argv[sys.argv.index('--exe') + 1]) if '--exe' in sys.argv else build()
    # --- start in « auto » (R46: the adapter's own starting mode)
    a, init, new = start(exe, '--start-mode', 'auto')
    res = new['result'] if new else {}
    check('--start-mode auto: session/new says currentModeId « auto »', res.get('modes', {}).get('currentModeId') == 'auto', res.get('modes'))
    mode_opt = next((o for o in res.get('configOptions', []) if o['id'] == 'mode'), {})
    check('--start-mode auto: the config option « mode » is « auto » and lists it', mode_opt.get('currentValue') == 'auto' and any(o['value'] == 'auto' for o in mode_opt.get('options', [])), mode_opt)
    sid = res.get('sessionId')
    r, _ = a.call('session/set_mode', {'sessionId': sid, 'modeId': 'default'})
    check('set_mode default after an « auto » start is accepted', r and 'result' in r, r)
    # vrai-autorisation in default mode: permission asked, allow -> French answer
    asked = []
    r, seen = prompt(a, sid, 'vrai-autorisation', lambda m: asked.append(m) or {'outcome': {'outcome': 'selected', 'optionId': 'allow'}})
    t = text_of(seen)
    check('vrai-autorisation (Demander): an announcement, then the permission with a diff, then the answer in French',
          r and r['result']['stopReason'] == 'end_turn' and len(asked) == 1 and asked[0]['params']['toolCall']['content'][0]['type'] == 'diff'
          and t.startswith('Je vais modifier') and "C'est fait" in t, (kinds(seen), t))
    r, seen = prompt(a, sid, 'vrai-autorisation', lambda m: {'outcome': {'outcome': 'selected', 'optionId': 'reject'}})
    check('vrai-autorisation: refused -> « je n\'ai rien modifié »', r and "je n'ai rien modifié" in text_of(seen), text_of(seen))
    r, seen = prompt(a, sid, 'vrai-autorisation', lambda m: {'outcome': {'outcome': 'cancelled'}})
    check('vrai-autorisation: cancelled -> stopReason cancelled', r and r['result']['stopReason'] == 'cancelled', r)
    a.close()

    # --- the other scenarios, ordinary start
    a, init, new = start(exe)
    sid = new['result']['sessionId']
    check('without --start-mode the session starts in « default » (unchanged)', new['result']['modes']['currentModeId'] == 'default')
    from_file = None
    r, seen = prompt(a, sid, 'vrai-tour')
    t = text_of(seen); k = kinds(seen)
    import re
    expected_tail = "Dites-moi si vous voulez le même espacement ailleurs (par exemple dans `div.plans`)."
    chunks = [m['params']['update']['content']['text'] for m in seen if m.get('method') == 'session/update' and m['params']['update'].get('sessionUpdate') == 'agent_message_chunk']
    check('vrai-tour: two announcements each before a tool call, then the long answer, end_turn',
          r and r['result']['stopReason'] == 'end_turn' and k.count('tool_call') == 2 and t.startswith("Je regarde d'abord") and t.endswith(expected_tail), k)
    check('vrai-tour: the answer has bare addresses, a table, two code blocks, in French',
          all(x in t for x in ('http://127.0.0.1:5183/', 'file:///C:/projet/styles/landing.css', 'https://example.test/docs/flex-gap?x=1&y=2#exemple', '| gap |', '```css', '```js'))
          and "C'est fait" in t)
    check('vrai-tour: streamed in many small chunks (>= 40), some cut an address or a fence', len(chunks) >= 40 and any(c.endswith(('ht', 'http', '``', '`')) or c.startswith(('tp', '`')) for c in chunks), len(chunks))
    r, seen = prompt(a, sid, 'vrai-markdown')
    check('vrai-markdown: only the long answer', r and text_of(seen).startswith("C'est fait.") and 'tool_call' not in kinds(seen))
    r, seen = prompt(a, sid, 'vrai-adresse')
    check('vrai-adresse: the bare addresses arrive whole', 'https://example.test/a_(b)?x=1&y=2.' in text_of(seen) and 'file:///C:/projet/styles/landing.css,' in text_of(seen))
    r, seen = prompt(a, sid, 'vrai-code')
    check('vrai-code: three fences, backticks inside, a 260-character line', text_of(seen).count('```') == 6 and 'x' * 260 in text_of(seen))
    r, seen = prompt(a, sid, 'vrai-erreur-outil')
    st = [m['params']['update'].get('status') for m in seen if m.get('method') == 'session/update' and m['params']['update'].get('sessionUpdate') == 'tool_call_update']
    check('vrai-erreur-outil: a failed tool call, then a completed one, then the answer', st == ['failed', 'completed'] and text_of(seen).endswith('`styles/landing.css`.'), st)
    t0 = time.time(); r, seen = prompt(a, sid, 'vrai-surcharge')
    err = (r or {}).get('error', {})
    check('vrai-surcharge: an announcement, then the JSON-RPC error with « 529 » and « overloaded_error »',
          text_of(seen).startswith('Je regarde') and '529' in json.dumps(err) and 'overloaded_error' in json.dumps(err), err)
    # vrai-lent: nothing for 2.5 s; cancelled during the tool call
    rid = a.next; t0 = time.time()
    a.send({'id': rid, 'method': 'session/prompt', 'params': {'sessionId': sid, 'prompt': [{'type': 'text', 'text': 'vrai-lent'}]}}); a.next += 1
    first = None; got = []; answer = None
    while time.time() - t0 < 15 and not answer:
        try:
            m = a.q.get(timeout=0.1)
        except queue.Empty:
            continue
        if m.get('id') == rid:
            answer = m; break
        got.append(m)
        if first is None and m.get('method') == 'session/update':
            first = time.time() - t0
        if m.get('method') == 'session/update' and m['params']['update'].get('sessionUpdate') == 'tool_call':
            a.send({'method': 'session/cancel', 'params': {'sessionId': sid}})
    check('vrai-lent: silence for about 2.5 s, then an announcement', first is not None and 2.3 <= first <= 4.0, first)
    check('vrai-lent: session/cancel during the tool call -> cancelled', answer and answer['result']['stopReason'] == 'cancelled', answer)
    # vrai-sourd: a cancel changes nothing, chunks go on (review M1)
    rid = a.next; a.next += 1; t0 = time.time()
    a.send({'id': rid, 'method': 'session/prompt', 'params': {'sessionId': sid, 'prompt': [{'type': 'text', 'text': 'vrai-sourd'}]}})
    time.sleep(0.6); a.send({'method': 'session/cancel', 'params': {'sessionId': sid}}); after = 0; answered = False
    while time.time() - t0 < 3.0:
        try:
            m = a.q.get(timeout=0.1)
        except queue.Empty:
            continue
        if m.get('id') == rid:
            answered = True
        elif time.time() - t0 > 1.2 and m.get('method') == 'session/update':
            after += 1
    check('vrai-sourd: after session/cancel the chunks go on, no answer', after >= 5 and not answered, (after, answered))
    a.close()
    a, init, new = start(exe); sid = new['result']['sessionId']
    # old scenarios unchanged
    r, seen = prompt(a, sid, 'reply')
    check('old scenario « reply » unchanged', text_of(seen) == 'Bonjour — voici la réponse.')
    r, seen = prompt(a, sid, 'annonce')
    check('old scenario « annonce » unchanged', text_of(seen).startswith('Je regarde le bouton.') and kinds(seen).count('tool_call') == 2)
    a.close()

    # --- --new-delay: session/new answered late
    a = Agent(exe, '--new-delay', '1200'); a.call('initialize', {'protocolVersion': 1, 'clientCapabilities': {}})
    t0 = time.time(); new, _ = a.call('session/new', {'cwd': str(ROOT), 'mcpServers': []})
    check('--new-delay 1200: session/new answered after about 1.2 s', new and 1.1 <= time.time() - t0 <= 3.0, time.time() - t0)
    a.close()

    failed = [n for n, ok in results if not ok]
    print(f'{len(results) - len(failed)} / {len(results)} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding='utf-8')
        except Exception:
            pass
    sys.exit(main())
