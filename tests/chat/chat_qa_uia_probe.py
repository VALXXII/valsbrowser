#!/usr/bin/env python3
"""Diagnosis (real window, fake agent, ~30 s): the UI Automation tree of the browser before the chat bubble exists,
with the bubble open, with it closed, and with the panel open. -> build/chat-qa/comment-v2/uia-probe.txt"""
from __future__ import annotations
import subprocess
import sys
import time
import traceback

import chat_qa as C
from chat_qa import Q

OUT = C.QA / 'comment-v2'


def dump(label):
    t0 = time.time()
    try:
        r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(C.ROOT / 'tests' / 'chat' / 'uia_dump.ps1'),
                            '-ProcessId', str(Q.our_pid())], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
        out = r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        out = 'TIMEOUT'
    return f'===== {label} ({time.time() - t0:.2f} s)\n{out}\n'


def main():
    OUT.mkdir(parents=True, exist_ok=True); text = ''
    fake = C.TOOLS / 'fake-acp-agent.exe'
    C.launch(extra_env={'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like'})
    try:
        time.sleep(1.0); C.shortcut('A'); C.wait_log(r'ANNOTATE mode on', 10); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        C.invoke('AnnotationLayoutComment'); time.sleep(0.8)
        text += dump('before the bubble')
        m = len(C.log_text()); C.click_page('.btn-primary'); C.wait_log(r'CHAT panel ready', 15, m); time.sleep(1.5)
        text += dump('bubble open')
        time.sleep(3.0); text += dump('bubble open, 3 s later')
        t0 = time.time(); found = C.uia('-Id', 'AnnotationLayoutComment'); text += f'===== uia.ps1 find with the bubble open: {found} ({time.time() - t0:.2f} s)' + chr(10)
        C.panel("(function(){var t=document.querySelector('[contenteditable=true]');if(t)t.focus();return 1})()"); C.key_panel('Escape', 'Escape', 27); time.sleep(0.8)
        text += dump('bubble closed')
        C.invoke('AnnotationAskAgent'); time.sleep(1.5)
        text += dump('panel open')
        time.sleep(3.0); text += dump('panel open, 3 s later')
        t0 = time.time(); found = C.uia('-Id', 'AnnotationLayoutComment'); text += f'===== uia.ps1 find with the panel open: {found} ({time.time() - t0:.2f} s)' + chr(10)
    except Exception:
        text += traceback.format_exc()
    finally:
        C.quit_all()
        (OUT / 'uia-probe.txt').write_text(text, encoding='utf-8')
    print(text)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
