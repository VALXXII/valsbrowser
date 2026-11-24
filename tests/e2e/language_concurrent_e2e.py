"""Real-app E2E: two windows (processes) of the SAME profile with the interface language changing between them.

Same isolation as tests/e2e/language_e2e.py (whose helpers it reuses): a disposable copy in build/e2e-language,
fresh processes, UI Automation patterns only (never keys, clicks or foreground changes), PrintWindow of our own
windows, only processes whose image lives in the fixture folder are touched.

  1. A and B start in French (no preference), same profile.
  2. B: Réglages › Langue › English  -> file says en.
  3. A (which last saw Français): its menu must check English (what is on disk), and an explicit « Français »
     must be written -> file says fr.  Then A chooses English again -> file says en.
  4. C starts while A and B are open: interface in English; the WebView2 engine of the profile is SHARED with
     A and B, so its language is the one the first window started it with. Reported, not asserted as English:
     the effective --lang of the engine's processes and the profile's engine-args record.
  5. All closed, D starts (session restore may reopen the other windows, each a new process): interface English
     and the engine now runs --lang=en-US. Every fixture window is closed at the end.
Usage: python tests/e2e/language_concurrent_e2e.py <build dir>
"""
import json, os, re, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import language_e2e as E

FR = {'names': {'Settings': 'Réglages', 'History': 'Historique', 'NewTab': 'Nouvel onglet'}, 'title': ' — Browser'}
EN = {'names': {'Settings': 'Settings', 'History': 'History', 'NewTab': 'New tab'}, 'title': ' — Browser'}


def engine_langs():
    """--lang= of every WebView2 process whose command line points into OUR fixture (read-only WMI query)."""
    ps = ("[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; Get-CimInstance Win32_Process -Filter \"Name='msedgewebview2.exe'\" | "
          "Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress")
    r = subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
    try:
        rows = json.loads(r.stdout or '[]')
    except json.JSONDecodeError:
        rows = []
    if isinstance(rows, dict):
        rows = [rows]
    fix = os.path.normcase(E.FIX)
    out = {}
    for row in rows:
        cmd = row.get('CommandLine') or ''
        if fix not in os.path.normcase(cmd):
            continue
        kind = (re.search(r'--type=(\S+)', cmd) or [None, 'browser'])[1]
        lang = (re.search(r'--lang=(\S+)', cmd) or [None, '(none)'])[1]
        out.setdefault(kind, set()).add(lang)
    return {k: sorted(v) for k, v in out.items()}


def engine_record():
    found = []
    for base, _, files in os.walk(E.FIX):
        if 'engine-args' in files:
            found.append((os.path.relpath(base, E.FIX), open(os.path.join(base, 'engine-args'), 'rb').read().decode('utf-8', 'replace').strip()))
    return found


def choose(step, p, hwnd, row, item, pending, want):
    r = E.ps(E.PS_CHOOSE.format(hwnd=int(hwnd), pid=p.pid, row=row, item=item))
    print(f'     {step} menu:', r, flush=True)
    E.check('error' not in r, f'{step}: {row} › {item} through UIA ({r.get("error", "ok")})')
    saved = E.read_pref() or ''
    E.check(f'language={want}' in saved, f'{step}: file says language={want} (file: {saved!r})')
    return r


def close_drawer(hwnd):
    E.ps(E.PS_HEAD.format(hwnd=int(hwnd)) + "\n$s = ById $win 'Settings'; if ($s) { $s.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }\n")


def main():
    if not os.path.isfile(os.path.join(E.SRC, 'browser.exe')):
        print('missing build', E.SRC); return 2
    if E.ours():
        print('a fixture browser is already running: close it first'); return 2
    if os.path.isdir(E.FIX): shutil.rmtree(E.FIX)
    shutil.copytree(E.SRC, E.FIX)
    os.makedirs(E.SHOTS, exist_ok=True)
    E.write_pref(None)
    procs = []
    try:
        a = E.start('c1-A-french'); procs.append(a); E.expect_window('c1-A-french', a[1], FR)
        b = E.start('c1-B-french'); procs.append(b); E.expect_window('c1-B-french', b[1], FR)
        print('     engine after A+B:', engine_langs(), engine_record(), flush=True)
        choose('c2-B', b[0], b[1], 'Langue', 'English', 'Au prochain démarrage.', 'en')
        close_drawer(b[1])
        r = choose('c3-A', a[0], a[1], 'Langue', 'Français', '', 'fr')
        E.check(r.get('before') == 'English', f'c3-A: A\'s row showed what B saved (English) before choosing (got {r.get("before")!r})')
        close_drawer(a[1])
        choose('c3-A-again', a[0], a[1], 'Langue', 'English', 'Au prochain démarrage.', 'en')
        close_drawer(a[1])
        c = E.start('c4-C-english'); procs.append(c); E.expect_window('c4-C-english', c[1], EN)
        langs, record = engine_langs(), engine_record()
        print('     engine with A(FR)+B(FR)+C(EN) open:', langs, record, flush=True)
        E.check(bool(langs), 'c4: the shared WebView2 engine of the profile is running')
    finally:
        for p, hwnd, log, size in reversed(procs):
            E.stop(p, hwnd, log, size)
    time.sleep(3)
    E.check(not E.ours(), 'c5: all fixture windows closed')
    log = os.path.join(E.FIX, 'prototype.log')
    text = open(log, 'rb').read().decode('utf-8', 'replace') if os.path.exists(log) else ''
    E.check('UI LANGUAGE engine shared with an open window of this profile: --lang=fr' in text, 'c4: C logged that the shared engine keeps --lang=fr until every window of the profile closes')
    d = E.start('c5-D-english')
    try:
        time.sleep(2)
        print(f'     windows after restart: {len(E.ours())} (session restore may reopen the other windows, same profile)', flush=True)
        E.expect_window('c5-D-english', d[1], EN)
        time.sleep(2)
        langs = engine_langs()
        print('     engine after restart (D alone):', langs, engine_record(), flush=True)
        all_langs = {l for v in langs.values() for l in v if l != '(none)'}
        E.check(all_langs == {'en-US'}, f'c5: after every window closed, the engine runs --lang=en-US only (got {sorted(all_langs)})')
    finally:
        E.stop(*d)
        for _, hwnd in E.ours():   # session-restored windows of OUR fixture only
            E.u32.PostMessageW(hwnd, 0x0010, 0, 0)
        for _ in range(50):
            if not E.ours(): break
            time.sleep(0.2)
    E.check(not E.ours(), 'end: every fixture window closed (only ours)')
    print(f'\n{E.failures} failure(s)')
    return 1 if E.failures else 0


if __name__ == '__main__':
    sys.exit(main())
