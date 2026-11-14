#!/usr/bin/env python3
"""V3-A experiment: the page frame's 8 % fill in the engine's shot and after its removal. Copy of
build/annotations-v3-preview (same guards as sandbox_qa_v2: redirected agents, BROWSER_ANNOTATIONS_TEST=1), the raw
engine image dumped by BROWSER_ANNOTATIONS_QA_RAW_DIR. For a settled hover and for a move + click in the same instant:
effective fill opacity in the raw shot, and the stored shot vs the engine's shot of the same clip with the mode off."""
import base64
import io
import json
import re
import socket
import subprocess
import sys
import time
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sandbox_qa_v2 as V   # noqa: E402
Q = V.Q
RAW = V.QA / 'raw'


def engine(clip):
    from PIL import Image
    r = Q.page_ws().call('Page.captureScreenshot', {'format': 'png', 'clip': clip, 'captureBeyondViewport': False, 'fromSurface': True})
    return Image.open(io.BytesIO(base64.b64decode(r['data']))).convert('RGB')


def compare(ref, img, inside):
    a, b = ref.load(), img.load(); worst = 0; over2 = 0; n = 0; alpha = []
    acc = (62, 103, 255)
    for y in range(ref.size[1]):
        for x in range(ref.size[0]):
            d = max(abs(p - q) for p, q in zip(a[x, y], b[x, y])); worst = max(worst, d); over2 += d > 2; n += 1
            if inside(x, y):
                for ch in range(3):
                    if abs(acc[ch] - a[x, y][ch]) > 60:
                        alpha.append((b[x, y][ch] - a[x, y][ch]) / (acc[ch] - a[x, y][ch]))
    alpha.sort()
    return {'worst': worst, 'over2': over2, 'pixels': n, 'fillAlphaMedian': round(alpha[len(alpha) // 2], 4) if alpha else None}


def main():
    from PIL import Image
    V.prepare_agents(); RAW.mkdir(parents=True, exist_ok=True)
    for f in RAW.glob('*.png'):
        f.unlink()
    server = subprocess.Popen([sys.executable, str(V.ROOT / 'sandbox/annotations/serve.py')], cwd=V.ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p = None; out = {}
    try:
        for _ in range(100):
            try:
                socket.create_connection(('127.0.0.1', 5173), timeout=0.3).close(); break
            except OSError:
                time.sleep(0.1)
        p = V.launch({'BROWSER_ANNOTATIONS_QA_RAW_DIR': str(RAW)})
        V.invoke('AnnotateButton'); time.sleep(0.8)
        if 'AnnotationConnectLater' in V.uia_list():
            V.invoke('AnnotationConnectLater'); time.sleep(0.5)
        for case in ('settled', 'instant'):
            for sel in ('button.btn-primary', 'h1'):
                V.mode(True); pos = V.rect_of(sel); ws = Q.page_ws(); mark = len(V.log_text())
                if case == 'settled':
                    ws.call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': pos[0], 'y': pos[1]}); time.sleep(0.5)
                Q.mouse('click', pos[0], pos[1])
                m = V.wait_log(r'ANNOTATE capture (\d+)x(\d+) [^\n]*clip (\{[^\n]*?\}); native frame', 4, mark)
                time.sleep(0.4)
                raws = sorted(RAW.glob('raw-*.png'), key=lambda f: f.stat().st_mtime)
                V.note_type('probe'); mark2 = len(V.log_text()); V.invoke('AnnotationAdd')
                w = V.wait_log(r'ANNOTATE written (\S+)/(ann_\w+) with shot', 5, mark2)
                V.mode(False); time.sleep(0.4)
                ws.call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': pos[0], 'y': pos[1]}); time.sleep(0.5)
                if not (m and w and raws):
                    out[f'{case} {sel}'] = 'no capture'; continue
                clip = json.loads(m.group(3))['clip']
                ref = engine(clip); raw = Image.open(raws[-1]).convert('RGB')
                stored = Image.open(next(iter(V.STORE.glob(f'*/{w.group(2)}'))) / 'shot.png').convert('RGB')
                el = Q.evaluate(f"(function(){{var r=document.querySelector({json.dumps(sel)}).getBoundingClientRect();return [r.left+scrollX,r.top+scrollY,r.width,r.height]}})()")
                k = ref.size[0] / clip['width']
                inside = lambda x, y: el[0] + 3 < clip['x'] + x / k < el[0] + el[2] - 3 and el[1] + 3 < clip['y'] + y / k < el[1] + el[3] - 3
                out[f'{case} {sel}'] = {'raw vs mode off': compare(ref, raw, inside), 'stored vs mode off': compare(ref, stored, inside)}
                ref.save(RAW / f'ref-{case}-{sel[:6]}.png'); stored.save(RAW / f'stored-{case}-{sel[:6]}.png')
        print(json.dumps(out, indent=1))
    finally:
        if p:
            V.quit_browser(p)
        server.terminate()


if __name__ == '__main__':
    main()
