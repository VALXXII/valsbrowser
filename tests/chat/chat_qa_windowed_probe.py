#!/usr/bin/env python3
"""Diagnosis (real window, ~25 s): the chat WebView hosted WINDOWED with a transparent background inside the bubble's
see-through window (BROWSER_CHAT_WINDOWED_PROBE=1). Are the 16 px corners transparent over the page, and does UI
Automation see the chat page? -> build/chat-qa/comment-v2/probe-fenetree-*.png, probe-fenetree-uia.txt"""
from __future__ import annotations
import subprocess
import sys
import time
import traceback

import chat_qa as C
from chat_qa import Q
import chat_qa_comment as CC

OUT = C.QA / 'comment-v2'


def main(tag='fenetree', extra=None):
    OUT.mkdir(parents=True, exist_ok=True); text = ''
    fake = C.TOOLS / 'fake-acp-agent.exe'
    env = {'BROWSER_CHAT_FAKE_AGENT': f'"{fake}" --resume --claude-like'}
    env.update(extra if extra is not None else {'BROWSER_CHAT_WINDOWED_PROBE': '1'})
    C.launch(extra_env=env)
    try:
        time.sleep(1.0); C.shortcut('A'); C.wait_log(r'ANNOTATE mode on', 10); time.sleep(1.2)
        if C.invoke('AnnotationConnectLater') or C.invoke('AnnotationConnectClose'):
            time.sleep(0.6)
        C.invoke('AnnotationLayoutComment'); time.sleep(0.8)
        m = len(C.log_text()); C.click_page('.btn-primary'); C.wait_log(r'CHAT panel ready', 15, m); time.sleep(2.0)
        C.ws_for(C.URL).call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': 600, 'y': 700}); time.sleep(0.4)
        from PIL import ImageGrab
        bw = CC.bubble_window()
        if bw:
            r = bw[3]; ImageGrab.grab(bbox=(r[0] - 12, r[1] - 12, r[2] + 12, r[3] + 12), all_screens=True).save(OUT / f'probe-{tag}-bulle.png')
            ImageGrab.grab(bbox=(r[0] - 2, r[1] - 2, r[0] + 30, r[1] + 30), all_screens=True).resize((256, 256), 0).save(OUT / f'probe-{tag}-coin.png')
        text += 'bubble ' + str(bw) + '\n'
        t0 = time.time()
        r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(C.ROOT / 'tests' / 'chat' / 'uia_dump.ps1'),
                            '-ProcessId', str(Q.our_pid()), '-Depth', '4'], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
        text += f'uia ({time.time() - t0:.2f} s)\n' + r.stdout
        t0 = time.time(); text += f"uia.ps1 find: {C.uia('-Id', 'AnnotationLayoutComment')} ({time.time() - t0:.2f} s)\n"
    except Exception:
        text += traceback.format_exc()
    finally:
        C.quit_all()
        (OUT / f'probe-{tag}-uia.txt').write_text(text, encoding='utf-8')
    print(text)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main(*(sys.argv[1:2] or []), {} if '--visual' in sys.argv else None)
