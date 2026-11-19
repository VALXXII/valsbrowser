#!/usr/bin/env python3
"""Acceptance probe (real window, ~25 s): the bubble's corners and its UI Automation tree.

    python tests/chat/chat_qa_windowed_probe.py            # windowed hosting (the default)
    python tests/chat/chat_qa_windowed_probe.py visuel --visual   # BROWSER_CHAT_VISUAL=1

Criteria (coordinator, 19 Nov 2026): corners identical to Paper (16 px, the page seen through them, no black corner, no
border line) AND a complete UIA tree (the chat page readable, the main window's search finds the band's buttons).
-> build/chat-qa/comment-v2/probe-<tag>-*.png, probe-<tag>-uia.txt"""
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
    env.update(extra or {})
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
        if bw:
            from PIL import Image
            img = Image.open(OUT / f'probe-{tag}-bulle.png').convert('RGB'); px = img.load(); x0, y0 = 12, 12   # the window's top-left in the capture
            w, h = bw[3][2] - bw[3][0], bw[3][3] - bw[3][1]
            corner = px[x0, y0]; outside = px[x0 - 6, y0 + h // 2]; inside = px[x0 + w // 2, y0 + 3]; mid = px[x0 + 3, y0 + 3]
            text += f'corner pixel {corner}, page beside {outside}, surface {inside}, 3 px in the corner {mid}\n'
            text += f"CORNER {'OK' if abs(sum(corner) - sum(outside)) <= 24 and abs(sum(inside) - 3 * 0x17) <= 12 else 'FAIL'} (the page seen through the corner, the surface #171717)\n"
        t0 = time.time()
        r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(C.ROOT / 'tests' / 'chat' / 'uia_dump.ps1'),
                            '-ProcessId', str(Q.our_pid()), '-Depth', '4'], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
        text += f'uia ({time.time() - t0:.2f} s)\n' + r.stdout
        t0 = time.time(); found = C.uia('-Id', 'AnnotationLayoutComment'); text += f"uia.ps1 find: {found} ({time.time() - t0:.2f} s)\n"
        import re
        counts = [int(x) for x in re.findall(r'chat element FindAll: (\d+)', r.stdout)]
        text += f"UIA {'OK' if found.startswith('FOUND') and counts and max(counts) > 5 else 'FAIL'} (band found from the main window; chat page elements {counts})\n"
    except Exception:
        text += traceback.format_exc()
    finally:
        C.quit_all()
        (OUT / f'probe-{tag}-uia.txt').write_text(text, encoding='utf-8')
    print(text)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main(*(sys.argv[1:2] or []), {} if '--visual' in sys.argv else None)
