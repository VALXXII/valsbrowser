#!/usr/bin/env python3
"""Annotations V2 — real run on the sandbox with a COPY of the preview build (build/annotations-qa-app, its own
profile, settings and store). Drives only that copy: page input through CDP (loopback debugging port enabled
for this copy only), native surfaces through UI Automation (tests/annotations/uia.ps1) and messages posted to
OUR windows, captures through PrintWindow. No SendInput, no other application touched.

    python -I tests/annotations/sandbox_qa_v2.py run        # whole scenario -> build/annotations-v2-qa/report.json + PNGs

Agent connection is redirected (BROWSER_ANNOTATIONS_CONNECT_ROOT = build/annotations-v2-qa/agent-configs); the
real %APPDATA%\\Claude and %USERPROFILE%\\.codex are fingerprinted before and after (any change = failure).
"""
from __future__ import annotations
import ctypes
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sandbox_qa as Q   # noqa: E402  (CDP, windows, PrintWindow helpers)

ROOT = HERE.parents[1]
QA = ROOT / 'build' / 'annotations-v2-qa'
Q.QA = QA
APP = Q.APP
AGENTS = QA / 'agent-configs'
STORE = APP / 'annotations'
LOG = APP / 'prototype.log'
REAL = [pathlib.Path(os.environ['APPDATA']) / 'Claude', pathlib.Path(os.environ['USERPROFILE']) / '.codex']
user32 = ctypes.windll.user32
report = {'steps': [], 'measures': {}}


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': detail})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail) if detail else ''), flush=True)
    return ok


def fingerprint():
    """Every file our connection code could write in the REAL folders: the two configuration files, any backup or
    temporary file named after us, the .mcpb work folder never lives there, Claude's extension listing."""
    out = {}
    targets = [REAL[0] / 'claude_desktop_config.json', REAL[1] / 'config.toml']
    for t in targets:
        out[str(t)] = (t.stat().st_size, t.stat().st_mtime_ns, hashlib.sha256(t.read_bytes()).hexdigest()) if t.exists() else 'absent'
    for d in REAL:
        ours = sorted(str(p) for p in d.rglob('*') if 'browser-annotations' in p.name or '.tmp-' in p.name) if d.exists() else []
        out[str(d) + '\\<files named browser-annotations*/.tmp-*>'] = ours
    ext = REAL[0] / 'Claude Extensions'
    out[str(ext) + '\\<listing>'] = sorted(x.name for x in ext.iterdir()) if ext.exists() else 'absent'
    return out


def full_listing():
    out = {}
    for d in REAL:
        for p in d.rglob('*') if d.exists() else []:
            if p.is_file() and p.suffix in ('.json', '.toml', '.bak', '.mcpb'):
                try:
                    out[str(p)] = (p.stat().st_size, p.stat().st_mtime_ns)
                except OSError:
                    pass
    return out


def log_text():
    try:
        return LOG.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return ''


def wait_log(pattern, timeout=10.0, after=0):
    end = time.time() + timeout
    while time.time() < end:
        text = log_text()[after:]
        m = re.search(pattern, text)
        if m:
            return m
        time.sleep(0.1)
    return None


def uia(*args):
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(HERE / 'uia.ps1'), '-ProcessId', str(Q.our_pid()), *args],
                       capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
    return r.stdout.strip()


def uia_list():
    return uia('-List')


def invoke(aid=None, name=None):
    out = uia('-Id', aid, '-Invoke') if aid else uia('-Name', name, '-Invoke')
    return out.startswith('INVOKED') or out.startswith('EXPANDED')


def mode_active():
    return 'AnnotationBanner' in uia_list()


def mode(on):
    if mode_active() != on:
        invoke('AnnotateButton'); time.sleep(0.6)
    return mode_active() == on


def visible(cls):
    return [w for w in Q.windows() if w[1] == cls]


def pw_shot(name):
    """PrintWindow composite of the browser and our popups; per-pixel-alpha (layered) windows are blended with
    their alpha instead of pasted on black. The modal's veil is drawn as black at 0.549."""
    from PIL import Image
    main = Q.main_window()
    base, r0 = Q.print_window(main)
    base = base.convert('RGB')
    GWL_EXSTYLE, WS_EX_LAYERED = -20, 0x80000
    for h, cls, title, rect in reversed([w for w in Q.windows() if w[0] != main]):
        if cls == 'BrowserAnnotationVeil':
            base = Image.blend(base, Image.new('RGB', base.size, (0, 0, 0)), 0x8C / 255); continue
        img, r = Q.print_window(h)
        if img is None:
            continue
        layered = user32.GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_LAYERED
        rgb = img.convert('RGB')
        if layered:   # PrintWindow gives no per-pixel alpha: our surfaces never paint pure black, so black = transparent
            from PIL import ImageChops
            mask = rgb.convert('L').point(lambda v: 255 if v > 0 else 0)
            base.paste(rgb, (r.left - r0.left, r.top - r0.top), mask)
        else:
            base.paste(rgb, (r.left - r0.left, r.top - r0.top))
    # Child windows (launcher, cards, history: per-pixel alpha children over the WebView) are in the main capture.
    QA.mkdir(parents=True, exist_ok=True)
    path = QA / f'{name}.png'; base.save(path); print(path)


def shot(name):
    time.sleep(0.45)   # let the motion settle (<= 300 ms everywhere)
    try:
        pw_shot(name)
    except Exception as e:
        print('shot failed', name, e)


def prepare_agents():
    shutil.rmtree(AGENTS, ignore_errors=True)
    claude = AGENTS / 'AppData' / 'Roaming' / 'Claude'; codex = AGENTS / 'home' / '.codex'
    (AGENTS / 'AppData' / 'Local').mkdir(parents=True); claude.mkdir(parents=True); codex.mkdir(parents=True)
    (claude / 'claude_desktop_config.json').write_text('{\n  "mcpServers": {\n    "autre-serveur": {"command": "C:\\\\outils\\\\autre.exe"}\n  },\n  "globalShortcut": "Ctrl+Space"\n}\n', encoding='utf-8')
    (codex / 'config.toml').write_text('# test\nmodel = "gpt-5-codex"\n\n[mcp_servers.autre-serveur]\ncommand = \'C:\\outils\\autre.exe\'\n', encoding='utf-8')
    return claude / 'claude_desktop_config.json', codex / 'config.toml'


def launch(extra_env=None, fresh=True):
    APP.mkdir(parents=True, exist_ok=True)
    for f in Q.SRC.iterdir():
        if f.name in ('profile', 'session', 'sessions', 'annotations', 'prototype.log', 'downloads', 'bookmarks', 'settings', 'history', 'annotations-connect'):
            continue
        dst = APP / f.name
        if f.is_dir():
            shutil.copytree(f, dst, dirs_exist_ok=True)
        else:
            shutil.copy2(f, dst)
    if fresh:
        for n in ('annotations', 'annotations-connect'):
            shutil.rmtree(APP / n, ignore_errors=True)
        for n in ('settings', 'prototype.log'):
            try:
                (APP / n).unlink()
            except OSError:
                pass
    shutil.rmtree(APP / 'sessions', ignore_errors=True)
    (APP / 'session').write_text('browser-session-2\n0\n1\n' + json.dumps(Q.URL) + '\n0\n', encoding='utf-8', newline='\n')
    env = dict(os.environ)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={Q.PORT} --remote-allow-origins=http://127.0.0.1:{Q.PORT}'
    env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(AGENTS)
    env['BROWSER_ANNOTATIONS_TEST'] = '1'
    env.pop('BROWSER_ANNOTATIONS_CONNECT_PATH', None)
    env.update(extra_env or {})
    # Never launch a copy that could reach the real configurations (incident 14 Nov 2026).
    assert env.get('BROWSER_ANNOTATIONS_TEST') == '1' and pathlib.Path(env['BROWSER_ANNOTATIONS_CONNECT_ROOT']).resolve().is_relative_to(ROOT / 'build'), 'unsafe environment'
    p = subprocess.Popen([str(APP / 'browser.exe')], cwd=str(APP), env=env)
    (APP / 'qa.pid').write_text(str(p.pid))
    reloads = 0
    for i in range(160):
        try:
            Q.page_ws(); break
        except BaseException:   # page_ws raises SystemExit while the page is about:blank or an error page
            time.sleep(0.3)
            # The first navigation of a fresh profile can be aborted while the bundled content blocker installs
            # (WebErrorStatus 9, error page): reload like a user would (F5 to our window).
            if 'ERROR PAGE' in log_text() and reloads < 3 and i % 10 == 9:
                reload_any(); reloads += 1
    time.sleep(1.5)
    return p


def reload_any():
    import urllib.request
    try:
        targets = json.loads(urllib.request.urlopen(f'http://127.0.0.1:{Q.PORT}/json', timeout=5).read())
        for t in targets:
            if t.get('type') == 'page':
                Q.WS(t['webSocketDebuggerUrl']).call('Page.navigate', {'url': Q.URL}); return True
    except Exception:
        pass
    return False


def quit_browser(p):
    h = Q.main_window()
    if h:
        user32.PostMessageW(h, 0x10, 0, 0)
    try:
        p.wait(timeout=20)
    except subprocess.TimeoutExpired:
        p.kill()


def rect_of(selector):
    return Q.evaluate(f"(function(){{var e=document.querySelector({json.dumps(selector)});if(!e)return null;e.scrollIntoView({{block:'center'}});var r=e.getBoundingClientRect();return [r.left+r.width/2,r.top+r.height/2,r.width,r.height]}})()")


def try_mcp(*args):
    r = subprocess.run([sys.executable, str(ROOT / 'sandbox/annotations/try-mcp.py'), *args, '--store', str(STORE), '--bridge', str(APP / 'browser-annotations-mcp.exe')],
                       capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    return r.returncode, r.stdout + r.stderr


def note_type(text):
    pop = [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote']
    if not pop:
        return False
    edit = Q.find_child(pop[0][0], 'Edit')
    user32.SendMessageW(edit, 0x0C, 0, ctypes.c_wchar_p(''))
    Q.post_text(text, edit)
    return True


def annotate_click(selector, note, name, expect_shot=True):
    """Click an element in annotation mode, measure the capture, type the note, Ajouter."""
    pos = rect_of(selector)
    if not pos:
        return step(f'{name}: element {selector}', False, 'not found')
    mark = len(log_text())
    ws = Q.page_ws(); ws.call('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': pos[0], 'y': pos[1]})
    ws.call('Input.dispatchMouseEvent', {'type': 'mousePressed', 'x': pos[0], 'y': pos[1], 'button': 'left', 'clickCount': 1})
    t_click = time.time() * 1000
    ws.call('Input.dispatchMouseEvent', {'type': 'mouseReleased', 'x': pos[0], 'y': pos[1], 'button': 'left', 'clickCount': 1})
    m = wait_log(r'ANNOTATE capture (\d+)x(\d+) (\d+) bytes \((\w+)\) ready (\d+) ms after the selection message[^\[]*\[[^\]]*image in memory (\d+) ms[^\]]*\] utc-ms=(\d+)|ANNOTATE capture unavailable \((\w+)\): ([^\n]*)', 5, mark)
    if m and m.group(1):
        shown, mem, at = int(m.group(5)), int(m.group(6)), int(m.group(7))
        total = at - t_click; host = total - shown
        rec = {'element': selector, 'size': f'{m.group(1)}x{m.group(2)}', 'bytes': int(m.group(3)), 'path': m.group(4),
               'clickToHostMs': round(host), 'imageInMemoryAfterClickMs': round(host + mem), 'shownInPanelAfterClickMs': round(total)}
        report['measures'].setdefault('capture', []).append(rec)
        step(f'{name}: capture at the click', True, f"{m.group(1)}x{m.group(2)} {m.group(4)}: image in memory {rec['imageInMemoryAfterClickMs']} ms after the click, shown {rec['shownInPanelAfterClickMs']} ms")
    else:
        step(f'{name}: capture at the click', not expect_shot, m.group(0) if m else 'no capture line')
    return pos


def main_run():
    QA.mkdir(parents=True, exist_ok=True)
    before = fingerprint(); listing_before = full_listing()
    claude_cfg, codex_cfg = prepare_agents()
    server = subprocess.Popen([sys.executable, str(ROOT / 'sandbox/annotations/serve.py')], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p = None
    try:
        import socket
        for _ in range(100):
            try:
                socket.create_connection(('127.0.0.1', 5173), timeout=0.3).close(); break
            except OSError:
                time.sleep(0.1)
        time.sleep(1.0)
        p = launch()
        # ---- launcher at rest, nothing read before any use
        ids = uia_list()
        step('launcher visible at launch (UIA AnnotationLauncher)', 'AnnotationLauncher' in ids, '')
        step('store untouched at launch (no annotations folder)', not STORE.exists())
        step('no ANNOTATE line before use', 'ANNOTATE' not in log_text())
        shot('0.1-launcher-at-rest')
        # ---- 1.1 activation without agent -> modal
        mark = len(log_text())
        invoke('AnnotateButton')
        ok = wait_log(r'ANNOTATE connect modal opened \(launch', 5, mark)
        step('1.1 activation without agent opens the modal (mode active behind)', ok and wait_log(r'ANNOTATE mode on', 1, mark), ok.group(0) if ok else '')
        shot('1.1-launch-modal')
        # ---- 1.8 Plus tard -> reminder, counter persisted
        invoke('AnnotationConnectLater'); time.sleep(0.5)
        later = wait_log(r'ANNOTATE connect: « Plus tard » \(1/3([^)]*)\)', 2)
        settings = (APP / 'settings').read_text(encoding='utf-8') if (APP / 'settings').exists() else ''
        report['laterPersistence'] = 'persisted' if later and not later.group(1) else 'session only (settings key annotationConnectDeclines not present yet)'
        step('« Plus tard »: counted (1/3); persistence = ' + report['laterPersistence'], later is not None, re.findall(r'annotation[a-zA-Z-]*=\S+', settings))
        step('« Plus tard »: reminder in the band (UIA AnnotationConnectReminder)', 'AnnotationConnectReminder' in uia_list())
        shot('1.8-band-reminder')
        # Re-activation in the same session: no modal
        mark = len(log_text())
        mode(False); mode(True); time.sleep(0.6)
        step('same session: the modal does not reopen', not wait_log(r'connect modal opened', 1.0, mark) and 'AnnotationConnectReminder' in uia_list())
        # ---- 2.1 annotate an element (capture at the click)
        annotate_click('button.btn-primary, button[type=submit]', 'x', '2.1 element « Payer »')
        time.sleep(0.4)
        shot('2.1-note-panel')
        invoke('AnnotationContext'); shot('2.2-pill-expanded')
        note_type('Aligner ce bouton à droite, comme la maquette')
        mark = len(log_text())
        t0 = time.time(); invoke('AnnotationAdd')
        w = wait_log(r'ANNOTATE written (\S+)/(ann_\w+) (with shot|\(shot joins when ready\)|without shot) in (\d+) ms', 5, mark)
        step('2.1 « Ajouter » writes at once with the shot', w and w.group(3) == 'with shot', w.group(0) if w else 'no write')
        if w:
            report['measures']['addWriteMs'] = int(w.group(4))
        first = w.group(2) if w else None
        time.sleep(0.6)
        shot('3.1-card-no-agent')
        step('3.1 card « Annotation ajoutée » without agent (UIA « Connecter un agent »)', 'Connecter un agent' in uia_list())
        if first:
            d = next(iter(STORE.glob(f'*/{first}')), None)
            js = json.loads((d / 'annotation.json').read_text(encoding='utf-8')) if d else {}
            from PIL import Image
            png = Image.open(d / 'shot.png') if d and (d / 'shot.png').exists() else None
            step('store: browser-annotation/2, site, shot.png', js.get('schema') == 'browser-annotation/2' and js.get('site') == 'http://127.0.0.1:5173' and png is not None,
                 f"{d.parent.name if d else ''} shot {png.size if png else None} crop={js.get('screenshot', {}).get('crop')} scale={js.get('screenshot', {}).get('scale')}")
            report['firstId'] = first
        # ---- second element: text selection-like small target (promo) + Annuler discards
        annotate_click('[data-testid="promo"], .promo, h1', 'x', '2.x second element (cancelled)')
        mark = len(log_text())
        pop = [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote']
        if pop:
            Q.post_key(0x1B, Q.find_child(pop[0][0], 'Edit'))   # Échap in the field: cancel (draft kept)
        time.sleep(0.6)
        step('cancel: nothing written, image dropped', not wait_log(r'ANNOTATE written', 0.5, mark) and len(list(STORE.glob('*/ann_*'))) == 1)
        # ---- capture time distribution: 10 more clicks (cancelled: nothing written)
        for k, sel in enumerate(['h1', 'button.btn-primary', '.summary', '#password', '.summary h2', 'button.btn-primary', 'h1', '.items', 'label', 'button.btn-primary']):
            mode(True)
            annotate_click(sel, 'x', f'timing {k + 1}')
            pop = [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote']
            if pop:
                Q.post_key(0x1B, Q.find_child(pop[0][0], 'Edit'))
            time.sleep(0.5)
        caps = [c for c in report['measures'].get('capture', []) if c['path'] == 'cdp']
        if caps:
            mem = sorted(c['imageInMemoryAfterClickMs'] for c in caps); shown = sorted(c['shownInPanelAfterClickMs'] for c in caps)
            report['measures']['captureSummary'] = {'n': len(caps), 'inMemoryMedian': mem[len(mem) // 2], 'inMemoryMax': mem[-1], 'shownMedian': shown[len(shown) // 2], 'shownMax': shown[-1],
                                                    'inMemoryUnder150': sum(1 for v in mem if v < 150)}
            step('capture timing (image in memory < 150 ms after the click)', mem[len(mem) // 2] < 150, report['measures']['captureSummary'])
        # ---- 2.4 « Ajouter » before the image is ready: the note leaves at once, the image joins it
        mode(True)
        pos = rect_of('button.btn-primary')
        mark = len(log_text())
        Q.mouse('click', pos[0], pos[1])
        edit = None
        for _ in range(100):
            pop = [w for w in Q.windows() if w[1] == 'BrowserAnnotationNote']
            if pop:
                edit = Q.find_child(pop[0][0], 'Edit')
                if edit:
                    break
            time.sleep(0.005)
        if edit:
            for ch in 'Tout de suite':
                user32.PostMessageW(edit, 0x102, ord(ch), 1)
            user32.PostMessageW(edit, 0x100, 0x0D, 1); user32.PostMessageW(edit, 0x101, 0x0D, 0xC0000001)
        w4 = wait_log(r'ANNOTATE written (\S+)/(ann_\w+) (with shot|\(shot joins when ready\)|without shot) in (\d+) ms', 5, mark)
        j4 = wait_log(r'ANNOTATE capture joined (ann_\w+): (\w+)', 5, mark)
        ok4 = w4 and w4.group(3) == '(shot joins when ready)' and j4 and j4.group(1) == w4.group(2) and j4.group(2) == 'ok'
        if ok4:
            d4 = next(iter(STORE.glob(f'*/{w4.group(2)}')), None)
            ok4 = d4 is not None and (d4 / 'shot.png').exists() and json.loads((d4 / 'annotation.json').read_text(encoding='utf-8')).get('screenshot', {}).get('present', True)
        step('2.4 « Ajouter » before the image: note written at once, image joins it', ok4 or (w4 and w4.group(3) == 'with shot'),
             (w4.group(0) if w4 else 'no write') + (' / ' + j4.group(0) if j4 else '') + ('' if ok4 else ' (image was already ready: case not reached)'))
        # ---- 2.3 capture impossible: a text selection scrolled out of view between the right-click and the menu choice
        mode(False)
        far = Q.evaluate("(function(){var t=document.getElementById('qa-far');if(!t){t=document.createElement('p');t.id='qa-far';t.style.marginTop='3000px';t.textContent='Texte du pied de page (QA)';document.body.appendChild(t);}"
                         "t.scrollIntoView({block:'center'});var r=document.createRange();r.selectNodeContents(t);var s=getSelection();s.removeAllRanges();s.addRange(r);var b=r.getBoundingClientRect();return [b.left+b.width/2,b.top+b.height/2]})()")
        Q.mouse('click', far[0], far[1], 'right'); time.sleep(0.8)
        Q.evaluate('window.scrollTo(0,0); 1')
        mark = len(log_text())
        menu_ok = invoke(name='Annoter la sélection')
        m = wait_log(r'ANNOTATE capture unavailable \((\w+)\): ([^\n]*)', 4, mark)
        time.sleep(0.5)
        step('2.3 capture impossible (selection out of view) -> « Capture impossible »', m is not None and 'AnnotationCaptureUnavailable' in uia_list(), (m.group(0) if m else f'menu invoked={menu_ok}'))
        shot('2.3-capture-unavailable')
        if visible('BrowserAnnotationNote'):
            note_type('Ce texte du pied de page est trop petit')
            mark = len(log_text()); invoke('AnnotationAdd')
            w2 = wait_log(r'ANNOTATE written \S+ (without shot|with shot|\(shot joins when ready\))', 4, mark)
            step('2.3 the annotation leaves without image', w2 and w2.group(1) == 'without shot', w2.group(0) if w2 else '')
        time.sleep(0.5)
        # ---- 1.4 / 1.3 connection on redirected folders
        mode(False); mode(True)
        mark = len(log_text())
        invoke('AnnotationConnectReminder'); time.sleep(0.8)
        step('banner « Connecter » opens the modal', wait_log(r'connect modal opened \(on demand', 2, mark) is not None)
        good = claude_cfg.read_text(encoding='utf-8')
        claude_cfg.write_text('{ "mcpServers": { "a": 1, }, }', encoding='utf-8')   # invalid JSON (trailing commas)
        mark = len(log_text()); invoke('AnnotationConnectClaude')
        f = wait_log(r'ANNOTATE connect claude: (\w+)', 6, mark)
        step('1.4 invalid config -> Échec, nothing written', f and f.group(1) == 'invalid_config' and claude_cfg.read_text(encoding='utf-8') == '{ "mcpServers": { "a": 1, }, }', f.group(0) if f else '')
        shot('1.4-connect-failed')
        claude_cfg.write_text(good, encoding='utf-8')
        mark = len(log_text()); invoke('AnnotationConnectClaude')
        f = wait_log(r'ANNOTATE connect claude: (\w+)[^\n]*', 6, mark)
        cfg = json.loads(claude_cfg.read_text(encoding='utf-8'))
        baks = list(claude_cfg.parent.glob('claude_desktop_config.json.browser-annotations-*.bak'))
        step('1.3 Claude connected (config merge, other keys kept, backup)', f and f.group(1) == 'connected' and 'browser-annotations-preview' in cfg.get('mcpServers', {}) and 'autre-serveur' in cfg['mcpServers'] and cfg.get('globalShortcut') == 'Ctrl+Space' and len(baks) == 1,
             f.group(0) if f else '')
        shot('1.3-connect-claude-connected')
        mark = len(log_text()); invoke('AnnotationConnectCodex')
        f = wait_log(r'ANNOTATE connect codex: (\w+)[^\n]*', 25, mark)
        toml = codex_cfg.read_text(encoding='utf-8')
        step('1.3 Codex connected (TOML table added, rest intact)', f and f.group(1) == 'connected' and '[mcp_servers.browser-annotations-preview]' in toml and toml.startswith('# test\nmodel = "gpt-5-codex"\n\n[mcp_servers.autre-serveur]'), f.group(0) if f else '')
        shot('1.3b-connect-both-connected')
        mark = len(log_text()); invoke('AnnotationDisconnectClaude')
        f = wait_log(r'ANNOTATE disconnect claude: (\w+)', 6, mark)
        cfg = json.loads(claude_cfg.read_text(encoding='utf-8'))
        step('« Déconnecter » removes only our entry', f and f.group(1) == 'disconnected' and 'browser-annotations-preview' not in cfg['mcpServers'] and 'autre-serveur' in cfg['mcpServers'] and claude_cfg.read_text(encoding='utf-8') == good,
             f.group(0) if f else '')
        shot('1.3c-after-disconnect-claude')
        invoke('AnnotationConnectDone'); time.sleep(0.6)
        step('« Terminé » closes; agent connected => no reminder', 'AnnotationConnectReminder' not in uia_list() and not visible('BrowserAnnotationVeil'))
        # ---- 3.x live cards with try-mcp (agent connected: card « Annotation ajoutée »)
        mode(True)
        annotate_click('button.btn-primary, button[type=submit]', 'x', '3.x element (agent connected)')
        note_type('Le libellé doit dire « Payer maintenant »')
        mark = len(log_text()); invoke('AnnotationAdd')
        w3 = wait_log(r'ANNOTATE written (\S+)/(ann_\w+)', 5, mark)
        time.sleep(0.4); shot('3.0-card-added-agent-connected')
        live = w3.group(2) if w3 else None
        if live:
            rc, out = try_mcp('claim', live)
            step('try-mcp claim', rc == 0, out.splitlines()[0] if out else '')
            time.sleep(1.5)
            shot('3.2-card-agent-working')
            names = uia_list()
            step('3.2 card « … s\'en occupe » + counter 1 (live, store watched)', "s'en occupe" in names and '1 en cours' in names, [l for l in names.splitlines() if 'Card' in l or 'Launcher' in l][:3])
            rc, out = try_mcp('resolve', live, 'Libellé', 'changé', 'dans', 'Checkout.tsx')
            time.sleep(1.5)
            shot('3.3-card-agent-done')
            names = uia_list()
            step('3.3 card « … a terminé » + « Recharger la page » (live)', 'a terminé' in names and 'Recharger la page' in names, out.splitlines()[0] if out else '')
        # ---- 4.x history
        mode(False)
        mark = len(log_text())
        Q.post_key(ord('H'), None, ctrl=True, shift=True)
        h = wait_log(r'ANNOTATE history opened \((\d+) annotations\)', 5, mark)
        via = 'Ctrl+Maj+H'
        if not h:   # a posted shortcut shares the keyboard state with whoever is typing on this PC: the launcher instead
            invoke('AnnotationLauncher'); via = 'launcher (Ctrl+Maj+H posted did not arrive)'
            h = wait_log(r'ANNOTATE history opened \((\d+) annotations\)', 5, mark)
        step('4.1 history opens (store read at opening) via ' + via, h is not None, h.group(0) if h else '')
        shot('4.1-history')
        if live:
            invoke(name='Le libellé doit dire « Payer maintenant »'); time.sleep(0.8)
            shot('4.2-history-detail')
            clip_before = subprocess.run(['powershell', '-NoProfile', '-Command', 'Get-Clipboard -Raw'], capture_output=True, text=True, encoding='utf-8').stdout
            invoke(name='Copier en Markdown'); time.sleep(0.5)
            clip = subprocess.run(['powershell', '-NoProfile', '-Command', 'Get-Clipboard -Raw'], capture_output=True, text=True, encoding='utf-8').stdout
            step('« Copier en Markdown » (full Markdown from the store)', live in clip and 'Payer maintenant' in clip, clip[:120].replace('\n', ' | '))
            mark = len(log_text()); invoke(name='Supprimer')
            t = wait_log(r'ANNOTATE history trash (ann_\w+): (\w+)', 5, mark)
            rc, out = try_mcp('list')
            step('4.4 Supprimer: immediate, gone for agents (trash)', t and t.group(2) == 'ok' and live not in out and any((STORE / 'trash').rglob(live)), t.group(0) if t else '')
            shot('4.5-history-toast-undo')
            mark = len(log_text()); invoke('AnnotationUndo')
            u = wait_log(r'ANNOTATE history restore (ann_\w+): (\w+)', 5, mark)
            rc, out = try_mcp('list')
            step('4.5 « Annuler » restores it (agents see it again)', u and u.group(2) == 'ok' and live in out, u.group(0) if u else '')
            time.sleep(0.6); shot('4.6-history-after-undo')
            if clip_before:
                subprocess.run(['powershell', '-NoProfile', '-Command', 'Set-Clipboard -Value $input'], input=clip_before, text=True, encoding='utf-8')
        mark = len(log_text())
        invoke(name="Retour à l'historique"); time.sleep(0.5)
        invoke(name="Fermer l'historique"); time.sleep(0.8)
        step('history closed -> store watch stopped (nothing open, no agent working)', wait_log(r'store watch stopped', 3, mark) is not None and 'AnnotationHistory' not in uia_list())
        # ---- the selection frame never in a stored shot (accent rgb(62,103,255): the frame lines and the label)
        from PIL import Image
        frames = {}
        for png in STORE.rglob('shot.png'):
            im = Image.open(png).convert('RGB'); n = sum(1 for p in (im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata()) if abs(p[0] - 62) < 14 and abs(p[1] - 103) < 14 and abs(p[2] - 255) < 14)
            frames[png.parent.name] = n
        step('no selection frame in any stored shot (accent-blue pixels = 0)', frames and all(v == 0 for v in frames.values()), frames)
        # ---- passwords never stored
        leaked = [str(p) for p in STORE.rglob('*') if p.is_file() and (b'MDP-SANDBOX' in p.read_bytes() or b'client@exemple' in p.read_bytes())]
        step('the prefilled password / e-mail never reach the store', not leaked, leaked)
        # ---- fullscreen hides the launcher
        Q.post_key(0x7A, None); time.sleep(1.0)
        hidden = 'AnnotationLauncher' not in uia_list()
        Q.post_key(0x7A, None); time.sleep(1.0)
        step('launcher hidden in fullscreen (F11) and back after', hidden and 'AnnotationLauncher' in uia_list())
        report['log'] = [l for l in log_text().splitlines() if 'ANNOTATE' in l or 'SETTINGS annotation' in l]
    except BaseException:
        step('driver', False, traceback.format_exc())
    finally:
        if p:
            quit_browser(p)
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
        after = fingerprint(); listing_after = full_listing()
        step('real Claude/Codex configurations unchanged (fingerprints before/after)', before == after, after)
        moved = {k: (listing_before.get(k), listing_after.get(k)) for k in set(listing_before) | set(listing_after) if listing_before.get(k) != listing_after.get(k)}
        report['fingerprints'] = {'before': before, 'after': after, 'otherFilesChangedMeanwhile': moved}
        (QA / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
        fails = [s for s in report['steps'] if not s['ok']]
        print(f"\n{len(report['steps'])} steps, {len(fails)} failed -> {QA / 'report.json'}")


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8')
        except Exception:
            pass
    if len(sys.argv) > 1 and sys.argv[1] == 'run':
        main_run()
    else:
        print(__doc__)
