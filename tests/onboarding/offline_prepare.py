"""The fake agents profiles of sandbox/annotations/try-onboarding.py never go on the network to install (25 Nov 2026: Fix's
« present » runs downloaded the real Node.js 24.22.3 and claude-agent-acp 0.107.3, 118 MB, from nodejs.org / npm).

Each profile is played on a never-displayed Windows desktop (nothing on the shared screen, no SendInput), through the
launcher itself (its sealed home, its fake installer and CLI, the launch-only fake ACP agent; its offline runtime from
sandbox/agents/offline_runtime.py):
  present   Claude Code installed and signed in: a message typed and sent — the session starts, the fake agent answers
  absent    nothing installed: « Installer » then « Se connecter » in the chat's block, then the message — the same
  network   nothing installed, the fake installer fails as without network: « Installer » — an error, nothing else
and asserts ZERO download: the browser never says it must prepare (« needs_prepare », « prepare »), no download or unpack
file and no adapter tree ever appear in the copy's <data>\\agents (polled every 0.25 s), node.exe is the same file at the end,
and the browser process never holds a connection to nodejs.org or registry.npmjs.org (polled every second).

Before anything is launched: the fake ACP agent is an absolute existing file, the launcher still seals the home and sets the
launch-only fake agent, port 5173 is free. During the run, any sign of a download stops our browser at once (by PID).

    python -X utf8 -I tests/onboarding/offline_prepare.py <build-dir> [present,absent,network]
Exit code 0 when every profile passes. Writes build/offline-prepare/report.json."""
import ctypes, ctypes.wintypes as W, json, os, pathlib, socket, subprocess, sys, tempfile, threading, time, urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests' / 'annotations')); sys.path.insert(0, str(ROOT / 'sandbox' / 'agents'))
import sandbox_qa as Q   # noqa: E402  (CDP WebSocket)
import offline_runtime as O   # noqa: E402

APP = pathlib.Path(sys.argv[1]).resolve()
PROFILES = (sys.argv[2] if len(sys.argv) > 2 else 'present,absent,network').split(',')
OUT = ROOT / 'build' / 'offline-prepare'; OUT.mkdir(parents=True, exist_ok=True)
report = {'build': str(APP), 'profiles': {}}
k32 = ctypes.WinDLL('kernel32', use_last_error=True); u32 = ctypes.WinDLL('user32', use_last_error=True)
u32.CreateDesktopW.restype = W.HANDLE
T0 = time.perf_counter()
def say(s): print(f'{time.perf_counter() - T0:8.2f}  {s}', flush=True)

# ---------------------------------------------------------------- SAFETY before anything is launched
fake = APP.parent / (APP.name + '-test') / 'fake-acp-agent.exe'
if not (APP / 'browser.exe').is_file() or not fake.is_absolute() or not fake.is_file():
    raise SystemExit(f'refused: the build or its fake ACP agent is missing ({fake})')
launcher_src = (ROOT / 'sandbox' / 'annotations' / 'try-onboarding.py').read_text(encoding='utf-8')
for must in ("'USERPROFILE': str(home)", "'BROWSER_CHAT_FAKE_AGENT_LAUNCH_ONLY': '1'", "offline_runtime.seed_and_guard(pathlib.Path(data_root), app)",
             "system + r'\\System32', system, system + r'\\System32\\WindowsPowerShell\\v1.0'"):
    if must not in launcher_src:
        raise SystemExit(f'refused: try-onboarding.py no longer has {must!r} (sealed home, fake agent, offline runtime)')
O.check_build(APP, O.read_pins())
BLOCKED = set()
for host in O.BLOCKED_HOSTS:   # their addresses, to recognise a connection (a name lookup only: no request is sent)
    try: BLOCKED |= {a[4][0] for a in socket.getaddrinfo(host, 443)}
    except OSError: pass

class SI(ctypes.Structure):
    _fields_ = [('cb', W.DWORD), ('lpReserved', W.LPWSTR), ('lpDesktop', W.LPWSTR), ('lpTitle', W.LPWSTR), ('dwX', W.DWORD), ('dwY', W.DWORD),
                ('dwXSize', W.DWORD), ('dwYSize', W.DWORD), ('dwXCountChars', W.DWORD), ('dwYCountChars', W.DWORD), ('dwFillAttribute', W.DWORD),
                ('dwFlags', W.DWORD), ('wShowWindow', W.WORD), ('cbReserved2', W.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', W.HANDLE), ('hStdOutput', W.HANDLE), ('hStdError', W.HANDLE)]
class PI(ctypes.Structure):
    _fields_ = [('hProcess', W.HANDLE), ('hThread', W.HANDLE), ('dwProcessId', W.DWORD), ('dwThreadId', W.DWORD)]

def port_open(p):
    try: socket.create_connection(('127.0.0.1', p), timeout=.3).close(); return True
    except OSError: return False


def play(profile):
    name = f'browser-essai-offline-{profile}'
    home = pathlib.Path(tempfile.gettempdir()) / name; run = home / 'app'; log = run / 'prototype.log'
    res = {'steps': [], 'downloads': [], 'blocked': [], 'prepareLines': []}
    def step(what, ok, detail=''):
        res['steps'].append({'step': what, 'ok': bool(ok), 'detail': detail}); say(('PASS ' if ok else 'FAIL ') + f'{profile}: {what}' + (f' — {detail}' if detail != '' else ''))
    if port_open(5173):
        step('port 5173 free (no other trial running)', False); return res
    desk = f'offline-{profile}'; hdesk = u32.CreateDesktopW(desk, None, None, 0, 0x10000000, None)
    s = socket.socket(); s.bind(('127.0.0.1', 0)); cdp = s.getsockname()[1]; s.close()
    env = dict(os.environ)
    for k in list(env):
        if k.upper().startswith(('BROWSER_CHAT_', 'BROWSER_ANNOTATIONS_', 'BROWSER_AGENT', 'BROWSER_FAKE', 'WEBVIEW2_')): env.pop(k)
    env['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = f'--remote-debugging-port={cdp} --remote-allow-origins=http://127.0.0.1:{cdp}'
    block = ''.join(f'{k}={v}\0' for k, v in env.items()) + '\0'
    agents = 'absent' if profile == 'network' else profile
    extra = ' --install-mode network' if profile == 'network' else ''
    out = home.parent / f'{name}-launcher.txt'
    cmd = (f'cmd.exe /c ""{sys.executable}" -X utf8 "{ROOT / "sandbox" / "annotations" / "try-onboarding.py"}" --app "{APP}" --agents {agents}{extra} '
           f'--install-delay-ms 1500 --name {name} > "{out}" 2>&1"')
    si = SI(); si.cb = ctypes.sizeof(si); si.lpDesktop = 'WinSta0\\' + desk; pi = PI()
    if not k32.CreateProcessW(None, ctypes.create_unicode_buffer(cmd), None, None, False, 0x08000000 | 0x00000400, ctypes.create_unicode_buffer(block), str(ROOT), ctypes.byref(si), ctypes.byref(pi)):
        step('launcher started', False, ctypes.get_last_error()); return res
    k32.CloseHandle(pi.hThread)
    say(f'{profile}: launcher started on a hidden desktop')

    def ours():
        ps = f"Get-CimInstance Win32_Process -Filter \"Name='browser.exe'\" | Where-Object {{ $_.ExecutablePath -like '{run}*' }} | ForEach-Object {{ $_.ProcessId }}"
        return [int(x) for x in subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True).stdout.split() if x.isdigit()]
    stop = threading.Event(); state = {'pids': [], 'seen': 0}
    def watch():   # every 0.25 s the copy's agents folder, every second the browser's connections, the log's prepare lines
        last_net = 0.0
        while not stop.is_set():
            d = O.downloads_in(run)
            if d and d[0] not in res['downloads']:
                res['downloads'] += d; say(f'!!! {profile}: a download in the copy: {d[:2]} — our browser stops now')
                for p in state['pids']: subprocess.run(['taskkill', '/T', '/F', '/PID', str(p)], capture_output=True)
            try: ls = log.read_text(encoding='utf-8', errors='replace').splitlines()
            except OSError: ls = []
            for l in ls[state['seen']:]:
                if 'needs_prepare' in l or l.startswith('CHAT prepare ') or 'downloaded' in l.lower():
                    res['prepareLines'].append(l[:200]); say(f'!!! {profile}: {l[:160]} — our browser stops now')
                    for p in state['pids']: subprocess.run(['taskkill', '/T', '/F', '/PID', str(p)], capture_output=True)
            state['seen'] = len(ls)
            if time.time() - last_net > 1.0 and state['pids'] and BLOCKED:
                last_net = time.time()
                ps = ("Get-NetTCPConnection -State Established -ErrorAction SilentlyContinue | Where-Object { @(" + ','.join(str(p) for p in state['pids']) +
                      ") -contains $_.OwningProcess } | ForEach-Object { $_.RemoteAddress }")
                for a in subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True).stdout.split():
                    if a in BLOCKED and a not in res['blocked']: res['blocked'].append(a); say(f'!!! {profile}: a connection to {a} (nodejs.org / npm)')
            time.sleep(0.25)
    def targets():
        try: return json.loads(urllib.request.urlopen(f'http://127.0.0.1:{cdp}/json', timeout=3).read())
        except Exception: return []
    ws = {}
    def ev(t, js):
        try:
            if t['id'] not in ws: ws[t['id']] = Q.WS(t['webSocketDebuggerUrl'])
            return ws[t['id']].call('Runtime.evaluate', {'expression': js, 'returnByValue': True}).get('result', {}).get('value')
        except Exception:
            ws.pop(t['id'], None); return None
    def chat():
        ts = [t for t in targets() if t.get('type') == 'page' and t.get('url', '').startswith('https://chat.browser.internal')]
        return ts[0] if ts else None
    def wait(pred, timeout, every=0.25):
        end = time.time() + timeout
        while time.time() < end:
            v = pred()
            if v: return v
            time.sleep(every)
        return None
    def logged(text):
        try: return any(text in l for l in log.read_text(encoding='utf-8', errors='replace').splitlines())
        except OSError: return False
    def main_hwnd():
        pids = set(state['pids']); found = []
        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def cb(h, _):
            c = ctypes.create_unicode_buffer(64); u32.GetClassNameW(h, c, 64); p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p))
            if c.value == 'LisiereBrowser' and p.value in pids: found.append(h)
            return True
        def body(): u32.SetThreadDesktop(W.HANDLE(hdesk)); u32.EnumWindows(cb, 0)
        th = threading.Thread(target=body); th.start(); th.join(); return found[0] if found else None
    watcher = threading.Thread(target=watch, daemon=True)
    try:
        page = wait(lambda: any('127.0.0.1:5173' in t.get('url', '') for t in targets()), 120)
        state['pids'] = ours(); watcher.start()
        step('the browser starts (the launcher\'s offline runtime in place)', page and state['pids'], open(out, encoding='utf-8', errors='replace').read()[-300:] if out.exists() else '')
        node = O.node_exe(run, O.read_pins()); node_before = (node.stat().st_mtime_ns, node.stat().st_size) if node.is_file() else None
        time.sleep(2.0)
        u32.PostMessageW(main_hwnd(), 0x8000 + 1, 3063, 0)   # Ctrl+Maj+D: the chat
        ct = wait(lambda: chat() if chat() and ev(chat(), "!!document.querySelector('.cin')") else None, 40)
        step('the chat opens', ct)
        if ct:
            ev(ct, "(()=>{const c=document.querySelector('.cin');c.focus();document.execCommand('insertText',false,'bonjour hors ligne');return 1})()")
            if profile in ('absent', 'network'):
                ok = wait(lambda: ev(ct, "!!Array.from(document.querySelectorAll('.tas-btn')).find(b=>b.dataset.tas==='claude'&&b.textContent==='Installer')"), 30)
                ev(ct, "Array.from(document.querySelectorAll('.tas-btn')).find(b=>b.dataset.tas==='claude'&&b.textContent==='Installer').click()")
                step('« Installer » (Claude Code) clicked in the chat\'s block', ok)
            if profile == 'absent':
                ok = wait(lambda: ev(ct, "!!Array.from(document.querySelectorAll('.tas-btn')).find(b=>b.dataset.tas==='claude'&&b.textContent==='Se connecter')"), 60)
                ev(ct, "Array.from(document.querySelectorAll('.tas-btn')).find(b=>b.dataset.tas==='claude'&&b.textContent==='Se connecter').click()")
                step('the fake install done, « Se connecter » clicked', ok)
            if profile in ('present', 'absent'):
                ready = wait(lambda: logged('session start: ready'), 60)
                step('the chat\'s session starts with no preparation (the local runtime)', ready)
                time.sleep(1.5)
                ev(ct, "document.querySelector('.cin').focus()")
                for typ in ('keyDown', 'keyUp'):
                    ws[ct['id']].call('Input.dispatchKeyEvent', {'type': typ, 'key': 'Enter', 'code': 'Enter', 'windowsVirtualKeyCode': 13, 'nativeVirtualKeyCode': 13})
                step('the message is sent and the fake agent answers', wait(lambda: logged('prompt sent') and logged('ended: end_turn'), 30))
            else:
                failed = wait(lambda: logged('AGENTS claude: error') or logged('AGENTS claude: failed') or logged('install claude: failed'), 30)
                step('the fake installer fails as without network (an error, nothing downloaded)', failed or logged('AGENTS claude'), '')
            time.sleep(3.0)   # the model read (#128 v2) and a late preparation would show now
        node_after = (node.stat().st_mtime_ns, node.stat().st_size) if node.is_file() else None
        step('node.exe is the seeded file, untouched', node_before and node_before == node_after, [node_before, node_after])
        step('no download or unpack file and no adapter tree ever in the copy', not res['downloads'] and not O.downloads_in(run), res['downloads'][:3])
        step('the browser never said it must prepare (needs_prepare / prepare)', not res['prepareLines'], res['prepareLines'][:3])
        step('no connection of the browser to nodejs.org / registry.npmjs.org', not res['blocked'], {'blocked': res['blocked'], 'watched': sorted(BLOCKED)[:4]})
    finally:
        stop.set()
        h = main_hwnd() if state['pids'] else None
        if h: u32.PostMessageW(h, 0x10, 0, 0)
        end = time.time() + 15
        while time.time() < end and ours(): time.sleep(0.5)
        for p in ours(): subprocess.run(['taskkill', '/T', '/F', '/PID', str(p)], capture_output=True)   # ours only, by PID
        k32.WaitForSingleObject(pi.hProcess, 20000); k32.CloseHandle(pi.hProcess)
        u32.CloseDesktop(W.HANDLE(hdesk))
    return res


ok_all = True
for prof in PROFILES:
    r = play(prof); report['profiles'][prof] = r
    ok_all = ok_all and r['steps'] and all(s['ok'] for s in r['steps'])
(OUT / 'report.json').write_text(json.dumps(report, indent=1, ensure_ascii=False, default=str), encoding='utf-8')
print('offline prepare:', 'every profile without any download' if ok_all else 'FAILED (build/offline-prepare/report.json)')
sys.exit(0 if ok_all else 1)
