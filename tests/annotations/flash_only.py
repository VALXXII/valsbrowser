#!/usr/bin/env python3
"""V3-A: the « no flash » probe alone (sandbox_qa_v2.flash_probe) on the QA copy of build/annotations-v3-preview."""
import json, socket, subprocess, sys, time, pathlib
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sandbox_qa_v2 as V   # noqa: E402

def main(control=False):
    V.prepare_agents()
    server = subprocess.Popen([sys.executable, str(V.ROOT / 'sandbox/annotations/serve.py')], cwd=V.ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p = None
    try:
        for _ in range(100):
            try:
                socket.create_connection(('127.0.0.1', 5173), timeout=0.3).close(); break
            except OSError:
                time.sleep(0.1)
        env = {'BROWSER_ANNOTATIONS_QA_NO_NATIVE_FRAME': '1'} if control else {}
        for a in sys.argv[1:]:
            if a.startswith('--env='):
                k, v = a[6:].split('=', 1); env[k] = v
        p = V.launch(env or None); V.topmost(True)
        V.invoke('AnnotateButton'); time.sleep(0.8)
        if 'AnnotationConnectLater' in V.uia_list():
            V.invoke('AnnotationConnectLater'); time.sleep(0.5)
        for sel in ('button.btn-primary', 'h1', '.summary h2'):
            V.mode(True); V.flash_probe(sel, ('control ' if control else '') + sel, control=control); V.close_note()
        for r in V.report['measures'].get('flashProbe', []):
            print({k: r[k] for k in ('element', 'frames', 'medianFrameMs', 'accentBefore', 'minAccent', 'framesBelow70pc', 'pageDarkBefore', 'pageDarkMin', 'pageFramesBelow70pc', 'pageLowAt')})
    finally:
        if p:
            V.topmost(False); V.quit_browser(p)
        server.terminate()

if __name__ == '__main__':
    main('--control' in sys.argv)
