#!/usr/bin/env python3
"""V3-A: capture timing only (no other step). Launches the QA copy of build/annotations-v3-preview on the sandbox
(same guards as sandbox_qa_v2: redirected agent folders, BROWSER_ANNOTATIONS_TEST=1), clicks N elements, prints the
host's breakdown of every capture, closes everything.

    python tests/annotations/capture_timing.py [N]
"""
import re
import socket
import subprocess
import sys
import time
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sandbox_qa_v2 as V   # noqa: E402

def main(n):
    V.prepare_agents()
    server = subprocess.Popen([sys.executable, str(V.ROOT / 'sandbox/annotations/serve.py')], cwd=V.ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p = None
    try:
        for _ in range(100):
            try:
                socket.create_connection(('127.0.0.1', 5173), timeout=0.3).close(); break
            except OSError:
                time.sleep(0.1)
        p = V.launch()
        V.invoke('AnnotateButton'); time.sleep(0.8)
        if 'AnnotationConnectLater' in V.uia_list():
            V.invoke('AnnotationConnectLater'); time.sleep(0.5)
        sels = ['h1', 'button.btn-primary', '.summary', '.summary h2', 'label', '.items']
        for k in range(n):
            V.mode(True)
            V.annotate_click(sels[k % len(sels)], 'x', f'timing {k + 1}')
            V.close_note()
        caps = [c for c in V.report['measures'].get('capture', [])]
        for line in V.log_text().splitlines():
            if 'ANNOTATE capture' in line:
                m = re.search(r'native frame[^\]]*', line); print(m.group(0) if m else line)
        mem = sorted(c['imageInMemoryAfterClickMs'] for c in caps)
        if mem:
            print(f'image in memory after the click: median {mem[len(mem) // 2]} ms, max {mem[-1]} ms, under 150 ms: {sum(1 for v in mem if v < 150)}/{len(mem)}')
    finally:
        if p:
            V.quit_browser(p)
        server.terminate()

if __name__ == '__main__':
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 8)
