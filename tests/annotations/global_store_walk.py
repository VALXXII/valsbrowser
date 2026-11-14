#!/usr/bin/env python3
"""Annotations are global (one store for every profile) — real run on a COPY of a build (default
build/annotations-merge-check, never build/annotations-preview), in the QA app folder of sandbox_qa_v2.py (V.APP) with two profiles
(Personnel = default, Travail). Same guards as sandbox_qa_v2.py: BROWSER_ANNOTATIONS_TEST=1, agent configurations
redirected under build/, the REAL %APPDATA%\\Claude and %USERPROFILE%\\.codex fingerprinted before and after, the
browser and the page server closed in a finally.

    python tests/annotations/global_store_walk.py [--src build/annotations-merge-check]
    -> build/annotations-v2-qa/global-store/{report.json, *.png}

Steps: an OLD per-profile store (written by bridge_fixture) sits in profiles\\ptravail01\\annotations before
launch; the first store job merges it into <app>\\annotations; an annotation is added in Personnel, the window
switches to Travail in place (--workspace-test « qa switch »), another annotation is added there: both land in
<app>\\annotations with their profile, nothing is written under profiles\\<id>\\; the bridge (try-mcp) shows
« Profil : Travail ». Each WebView2 engine gets its own DevTools port (--remote-debugging-port=0, read back from
its DevToolsActivePort file) so the two profiles' pages can both be driven.
"""
from __future__ import annotations
import argparse
import ctypes
import ctypes.wintypes as W
import json
import pathlib
import shutil
import socket
import subprocess
import sys
import time
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import sandbox_qa_v2 as V   # noqa: E402

Q = V.Q
ROOT = V.ROOT
APP = V.APP
OUT = ROOT / 'build' / 'annotations-v2-qa' / 'global-store'
STORE = APP / 'annotations'
TRAVAIL = 'ptravail01'
user32 = ctypes.windll.user32
report = {'steps': []}


def step(name, ok, detail=''):
    report['steps'].append({'step': name, 'ok': bool(ok), 'detail': detail})
    print(('PASS ' if ok else 'FAIL ') + name + (' — ' + str(detail) if detail else ''), flush=True)
    V.report['steps'].append({'step': name, 'ok': bool(ok)})
    return ok


class CDS(ctypes.Structure):
    _fields_ = [('dwData', W.WPARAM), ('cbData', W.DWORD), ('lpData', ctypes.c_void_p)]


def qa(cmd):
    """« PROF » « qa <cmd> » to our window (--workspace-test hooks, src/profile-workspace.h)."""
    h = Q.main_window()
    data = ctypes.create_unicode_buffer('qa ' + cmd)
    cds = CDS(0x464F5250, ctypes.sizeof(data), ctypes.cast(data, ctypes.c_void_p))
    result = W.DWORD()
    user32.SendMessageTimeoutW(h, 0x004A, 0, ctypes.byref(cds), 2, 5000, ctypes.byref(result))
    return result.value


def devtools_port(user_data, timeout=40):
    end = time.time() + timeout
    while time.time() < end:
        for f in pathlib.Path(user_data).rglob('DevToolsActivePort'):
            try:
                port = int(f.read_text().splitlines()[0])
                socket.create_connection(('127.0.0.1', port), timeout=0.5).close()
                return port
            except (OSError, ValueError, IndexError):
                pass
        time.sleep(0.3)
    return 0


def wait_page(timeout=40):
    end = time.time() + timeout
    while time.time() < end:
        try:
            Q.page_ws(); return True
        except BaseException:
            time.sleep(0.3)
    return False


def entries():
    out = []
    for f in STORE.glob('*/*/annotation.json'):
        if f.parent.parent.name in ('trash', 'readers'):
            continue
        try:
            out.append(json.loads(f.read_text(encoding='utf-8')))
        except ValueError:   # the fixture's deliberately corrupt entry: listed by folder only
            out.append({'id': f.parent.name, 'corrupt': True})
    return out


def seed(src, fixture):
    # A fresh QA app: the build's files only (no profile, session, store or registry of an earlier run).
    for n in ('annotations', 'annotations-connect', 'profiles', 'sessions', 'windows'):
        shutil.rmtree(APP / n, ignore_errors=True)
    for n in ('profiles.txt', 'settings', 'prototype.log', 'session', 'qa.pid'):
        try:
            (APP / n).unlink()
        except OSError:
            pass
    APP.mkdir(parents=True, exist_ok=True)
    for f in (APP / 'profile').rglob('DevToolsActivePort'):   # a port file left by an earlier run is never read
        f.unlink(missing_ok=True)
    skip = {'profile', 'session', 'sessions', 'annotations', 'prototype.log', 'downloads', 'bookmarks', 'settings', 'history',
            'annotations-connect', 'profiles', 'profiles.txt', 'windows'}
    for f in src.iterdir():
        if f.name in skip:
            continue
        if f.is_dir():
            shutil.copytree(f, APP / f.name, dirs_exist_ok=True)
        else:
            shutil.copy2(f, APP / f.name)
    nl = '\n'
    (APP / 'profiles.txt').write_text('browser-profiles 1\nlast=default\np=default|house|0|0|1|Personnel\n'
                                      f'p={TRAVAIL}|briefcase|150|140|0|Travail\n', encoding='utf-8', newline=nl)
    session = 'browser-session-2\n0\n1\n' + json.dumps(Q.URL) + '\n0\n'
    (APP / 'session').write_text(session, encoding='utf-8', newline=nl)
    trav = APP / 'profiles' / TRAVAIL
    trav.mkdir(parents=True)
    (trav / 'session').write_text(session, encoding='utf-8', newline=nl)
    for d in (APP, trav):
        (d / 'settings').write_text('browser-settings 1\nrestore-tabs=1\n', encoding='utf-8', newline=nl)
    # The OLD per-profile store of Travail (what earlier builds wrote): the bridge transcripts' fixture.
    legacy = trav / 'annotations'
    r = subprocess.run([str(fixture), str(legacy)], capture_output=True, text=True)
    ids = sorted(p.name for p in legacy.glob('*/*') if (p / 'annotation.json').exists())
    return r.returncode == 0 and len(ids) > 0, ids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default=str(ROOT / 'build' / 'annotations-merge-check'))
    a = ap.parse_args()
    src = pathlib.Path(a.src).resolve()
    assert src.name != 'annotations-preview', "never the user's sandbox build"
    OUT.mkdir(parents=True, exist_ok=True)
    fixture = OUT / 'bridge_fixture.exe'
    gxx = ROOT / 'tools' / 'w64devkit' / 'bin' / 'g++.exe'
    subprocess.run([str(gxx), '-std=c++17', '-O2', '-static', '-municode', '-Isrc', 'tests/annotations/bridge_fixture.cpp', 'src/annotations.cpp',
                    '-o', str(fixture), '-lbcrypt'], cwd=ROOT, check=True)
    before = V.fingerprint()
    V.prepare_agents()
    server = subprocess.Popen([sys.executable, str(ROOT / 'sandbox/annotations/serve.py')], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p = None
    try:
        for _ in range(100):
            try:
                socket.create_connection(('127.0.0.1', 5173), timeout=0.3).close(); break
            except OSError:
                time.sleep(0.1)
        ok, legacy_ids = seed(src, fixture)
        step('old per-profile store seeded in profiles\\ptravail01\\annotations', ok, f'{len(legacy_ids)} annotations')
        import os
        env = dict(os.environ)
        env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = '--remote-debugging-port=0 --remote-allow-origins=*'
        env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(V.AGENTS)
        env['BROWSER_ANNOTATIONS_TEST'] = '1'
        env.pop('BROWSER_ANNOTATIONS_CONNECT_PATH', None)
        assert env['BROWSER_ANNOTATIONS_TEST'] == '1' and pathlib.Path(env['BROWSER_ANNOTATIONS_CONNECT_ROOT']).resolve().is_relative_to(ROOT / 'build'), 'unsafe environment'
        p = subprocess.Popen([str(APP / 'browser.exe'), '--workspace-test', '--profile', 'default'], cwd=str(APP), env=env)
        (APP / 'qa.pid').write_text(str(p.pid))
        Q.PORT = devtools_port(APP / 'profile')
        step('Personnel window up (its own DevTools port)', Q.PORT > 0 and wait_page(), f'port {Q.PORT}')
        time.sleep(1.5)

        # Personnel: the mode's first store job merges the old store, then an annotation is added.
        step('mode on (Personnel)', V.mode(True))
        V.annotate_click('h1', 'x', 'Personnel: element')
        V.note_type('Note ajoutée depuis Personnel'); mark = len(V.log_text()); V.invoke('AnnotationAdd')
        step('Personnel: annotation written', V.wait_log(r'ANNOTATE written', 8, mark) is not None)
        log = V.log_text()
        step('old store merged into the global store (log)', 'per-profile stores merged into' in log and 'old folders removed' in log,
             next((l for l in log.splitlines() if 'per-profile stores merged' in l), 'no merge line'))
        got = {e['id'] for e in entries()}
        step('every old annotation is now in <app>\\annotations', set(legacy_ids) <= got, f'{len(set(legacy_ids) & got)}/{len(legacy_ids)}')
        step('the old per-profile store is gone', not (APP / 'profiles' / TRAVAIL / 'annotations').exists())
        V.mode(False); time.sleep(0.6)

        # Same window, switched to Travail in place: same store.
        r = qa(f'switch {TRAVAIL}')
        step('window switched to Travail in place (qa switch)', r == 1 and V.wait_log(rf'WSQA switch {TRAVAIL}', 10) is not None, f'answer {r}')
        Q.PORT = devtools_port(APP / 'profiles' / TRAVAIL / 'profile')
        step('Travail page up (its own engine and DevTools port)', Q.PORT > 0 and wait_page(), f'port {Q.PORT}')
        time.sleep(1.0)
        step('mode on (Travail)', V.mode(True))
        V.annotate_click('button.btn-primary, button[type=submit]', 'x', 'Travail: element')
        V.note_type('Note ajoutée depuis Travail'); mark = len(V.log_text()); V.invoke('AnnotationAdd')
        step('Travail: annotation written', V.wait_log(r'ANNOTATE written', 8, mark) is not None)
        V.mode(False); time.sleep(0.6)

        mine = [e for e in entries() if e.get('note', '').startswith('Note ajoutée depuis')]
        prof = {e['note']: (e.get('source') or {}).get('profile') for e in mine}
        step('both new annotations are in the ONE store <app>\\annotations', len(mine) == 2, json.dumps(prof, ensure_ascii=False))
        step('profile recorded: Personnel', prof.get('Note ajoutée depuis Personnel') == {'id': 'default', 'name': 'Personnel'})
        step('profile recorded: Travail', prof.get('Note ajoutée depuis Travail') == {'id': TRAVAIL, 'name': 'Travail'})
        step('nothing under profiles\\<id>\\annotations', not list((APP / 'profiles').glob('*/annotations')))
        V.QA = OUT; V.shot('travail-after-add')

        # The bridge renders the profile (summary JSON + Markdown).
        trav = next((e for e in mine if e['note'].endswith('Travail')), None)
        if trav:
            code, out = V.try_mcp('get', trav['id'])
            (OUT / 'bridge-get.txt').write_text(out, encoding='utf-8')
            step('bridge get: « Profil : Travail » in the Markdown', code == 0 and '- Profil : `Travail`' in out)
            code, out = V.try_mcp('list', 'all')
            (OUT / 'bridge-list.txt').write_text(out, encoding='utf-8')
            step('bridge list answers over the global store', code == 0 and trav['id'] in out)
    except Exception:
        step('no exception', False, traceback.format_exc())
    finally:
        if p is not None:
            V.quit_browser(p)
            try:
                p.wait(timeout=20)
            except subprocess.TimeoutExpired:
                p.kill()
            step('browser closed', p.poll() is not None)
        server.kill()
        after = V.fingerprint()
        step('REAL Claude / Codex configurations unchanged', before == after)
        (OUT / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    failed = [s for s in report['steps'] if not s['ok']]
    print(f"{len(report['steps']) - len(failed)}/{len(report['steps'])} steps passed")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
