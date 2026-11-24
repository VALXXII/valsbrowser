#!/usr/bin/env python3
"""The annotation mode's general test (design/annotation-plan-2026-11-24.md, §4): ONE list of suites, run in full.

    python tests/annotation-general/run.py --day               # the windowless part: no window on the screen (about 15 min)
    python tests/annotation-general/run.py --night --build     # everything, real windows: the night round (about 2 h)
    python tests/annotation-general/run.py --list | --check    # the list / the manifest and the coverage table, nothing run

    --build        (night) builds the exact commit first: tools/release/build.py --out build/annotation-general --channel dev --preview
    --only a,b     runs only those suites, to look for a defect. The result is then « PARTIEL, non valable » and the exit
                   code is 3, whatever passed: a filtered run never stands for the general test.

The rules (plan §4.1) — why this file exists:
  1. one list, tests/annotation-general/manifest.json, run in its order;
  2. green = every suite of the part ran and passed. Absent, filtered, timed out, skipped = red;
  3. nothing is run twice: a failure that passes the second time is a failure;
  4. the report says the commit, the version and whether the tree was clean; only a clean, full night run stands for a release;
  5. coverage.json: every line of the inventory points to a test of a suite of the list, or to a gap planned for a lot that
     is not done yet. A line without a test, or a gap of a finished lot, = red;
  6. thresholds are in the manifest: « bloquant » fails the run, « objectif » (owner's decision 5) is measured and reported;
     a suite may only report an objective the manifest names;
  7. the report lists the test files changed since the last green run;
  8. safety: every suite runs in a Job Object (KILL_ON_JOB_CLOSE): nothing survives it, a process left behind is a failure;
     a browser.exe started from anywhere but this repository's build/ folder is a failure (never the installed browser,
     never the owner's profile); the real Claude / Codex configuration files are fingerprinted before and after.

Pause (the owner needs the screen): create the file build/annotation-general/PAUSE. The suite that runs is finished, the
next one waits until the file is removed; the wait is no one's time limit. A suite that was running when the file appeared
may have been touched by the owner's window: it is run once more after the pause and both results are in the report (the
first one is set aside, « à rejouer (fenêtre du propriétaire) ») — the only case where a suite runs twice.

Outputs: build/annotation-general/<day|night>/report.md, report.json, logs/<suite>.log.
Exit code: 0 green · 1 red · 2 could not start (locked session, missing tool, bad manifest) · 3 partial (--only).
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as W
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / 'build' / 'annotation-general'
BUILD_NAME = 'annotation-general-app'           # build/annotation-general-app (+ -test: the fake agent)
PY = sys.executable
PS = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File']
REQUIRED_IDS = ([f'C{i}' for i in range(1, 4)] + [f'E{i}' for i in range(1, 6)] + [f'M{i}' for i in range(1, 17)] +
                [f'F{i}' for i in range(1, 12)] + [f'R{i}' for i in range(1, 49)] + ['R39b'] +
                [f'O{i}' for i in range(1, 13)] + ['#129', '#130'])

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding='utf-8')
    except Exception:
        pass


# ---------------------------------------------------------------- Job Object (rule 8)
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
JOB_KILL_ON_CLOSE = 0x2000
JobObjectBasicProcessIdList, JobObjectExtendedLimitInformation = 3, 9
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


class _BasicLimit(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64), ('LimitFlags', W.DWORD),
                ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', W.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', W.DWORD), ('SchedulingClass', W.DWORD)]


class _IoCounters(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint64) for n in ('ReadOperationCount', 'WriteOperationCount', 'OtherOperationCount',
                                               'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]


class _ExtendedLimit(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', _BasicLimit), ('IoInfo', _IoCounters), ('ProcessMemoryLimit', ctypes.c_size_t),
                ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


class _PidList(ctypes.Structure):
    _fields_ = [('NumberOfAssignedProcesses', W.DWORD), ('NumberOfProcessIdsInList', W.DWORD), ('ProcessIdList', ctypes.c_size_t * 1024)]


k32.CreateJobObjectW.restype = W.HANDLE
k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, W.LPCWSTR]
k32.SetInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]
k32.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
k32.TerminateJobObject.argtypes = [W.HANDLE, W.UINT]
k32.QueryInformationJobObject.argtypes = [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD, ctypes.c_void_p]
k32.OpenProcess.restype = W.HANDLE
k32.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
k32.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, ctypes.POINTER(W.DWORD)]
k32.CloseHandle.argtypes = [W.HANDLE]
k32.GetCurrentProcess.restype = W.HANDLE
ntdll = ctypes.WinDLL('ntdll')
ntdll.NtResumeProcess.argtypes = [W.HANDLE]
CREATE_SUSPENDED = 0x00000004


class Job:
    """A Job Object that kills everything in it when its handle closes — this process ending, however it ends."""
    def __init__(self):
        self.h = k32.CreateJobObjectW(None, None)
        if not self.h:
            raise OSError(ctypes.get_last_error(), 'CreateJobObjectW')
        info = _ExtendedLimit(); info.BasicLimitInformation.LimitFlags = JOB_KILL_ON_CLOSE
        if not k32.SetInformationJobObject(self.h, JobObjectExtendedLimitInformation, ctypes.byref(info), ctypes.sizeof(info)):
            raise OSError(ctypes.get_last_error(), 'SetInformationJobObject')

    def add(self, popen):
        """`popen` was created suspended: it joins the job before it runs anything (nothing it starts can escape), then runs."""
        handle = W.HANDLE(int(popen._handle))
        ok = k32.AssignProcessToJobObject(self.h, handle); error = ctypes.get_last_error()
        ntdll.NtResumeProcess(handle)
        if not ok:
            raise OSError(error, 'AssignProcessToJobObject')

    def pids(self):
        lst = _PidList()
        if not k32.QueryInformationJobObject(self.h, JobObjectBasicProcessIdList, ctypes.byref(lst), ctypes.sizeof(lst), None):
            return []
        return [int(lst.ProcessIdList[i]) for i in range(min(lst.NumberOfProcessIdsInList, 1024))]

    def kill(self):
        k32.TerminateJobObject(self.h, 1)

    def close(self):
        if self.h:
            k32.CloseHandle(self.h); self.h = None


def image_of(pid):
    h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return ''
    try:
        buf = ctypes.create_unicode_buffer(1024); n = W.DWORD(1024)
        return buf.value if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)) else ''
    finally:
        k32.CloseHandle(h)


# ---------------------------------------------------------------- the manifest and the coverage table
def load():
    manifest = json.loads((HERE / 'manifest.json').read_text(encoding='utf-8'))
    coverage = json.loads((HERE / 'coverage.json').read_text(encoding='utf-8'))
    return manifest, coverage


def check_static(manifest, coverage):
    """Rules 1, 5 and 6 on the files themselves. Returns the list of what is wrong (empty = fine)."""
    bad = []
    ids = [s['id'] for s in manifest['suites']]
    if len(ids) != len(set(ids)):
        bad.append('manifest: two suites share an id')
    for s in manifest['suites']:
        if s.get('part') not in ('day', 'night'):
            bad.append(f"manifest: suite {s['id']} has no part (day / night)")
        if not s.get('cmd') or not isinstance(s.get('timeout'), int):
            bad.append(f"manifest: suite {s['id']} needs a command and a time limit")
        script = next((a for a in s.get('cmd', []) if a.endswith(('.py', '.ps1'))), None)
        if script and not (ROOT / script).exists() and s.get('pendingMerge'):
            continue   # its script arrives with a branch not merged yet: said « en attente de fusion » at each run, never counted as passed
        if script and not (ROOT / script).exists():
            bad.append(f"manifest: suite {s['id']} names a script that does not exist ({script})")
    kinds = {t['id']: t.get('kind') for t in manifest.get('thresholds', [])}
    for tid, kind in kinds.items():
        if kind not in ('bloquant', 'objectif'):
            bad.append(f'manifest: threshold {tid} is neither « bloquant » nor « objectif »')
    for t in manifest.get('thresholds', []):
        if not t.get('decision'):
            bad.append(f"manifest: threshold {t['id']} does not say which decision sets it")
    done = set(manifest.get('lotsDone', []))
    lines = coverage.get('lines', {})
    for need in REQUIRED_IDS:
        if need not in lines:
            bad.append(f'coverage: the inventory line {need} is missing')
    for lid, line in lines.items():
        tests, gap = line.get('tests') or [], line.get('gap')
        if not tests and not gap:
            bad.append(f'coverage: {lid} has neither a test nor a planned gap')
        for t in tests:
            if t.get('suite') not in ids:
                bad.append(f"coverage: {lid} points to a suite that is not in the manifest ({t.get('suite')})")
            if not t.get('what'):
                bad.append(f'coverage: {lid} does not say which test of {t.get("suite")} proves it')
        if gap and not tests:
            if not isinstance(gap.get('lot'), int) or not gap.get('why'):
                bad.append(f'coverage: the gap of {lid} needs its lot and its reason')
            elif gap['lot'] in done:
                bad.append(f"coverage: {lid} still has no test although lot {gap['lot']} is marked done")
    return bad


# ---------------------------------------------------------------- git, fingerprints, session
def git(*args):
    r = subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
    return r.stdout.strip()


def identity():
    commit = git('rev-parse', 'HEAD'); count = git('rev-list', '--count', 'HEAD')
    dirty = [l for l in git('status', '--porcelain').splitlines() if l.strip()]
    return {'commit': commit, 'version': f'0.3.{count}', 'branch': git('rev-parse', '--abbrev-ref', 'HEAD'), 'dirty': dirty}


def real_configs():
    home = pathlib.Path(os.environ.get('USERPROFILE', str(pathlib.Path.home())))
    appdata = pathlib.Path(os.environ.get('APPDATA', str(home / 'AppData' / 'Roaming')))
    return [appdata / 'Claude' / 'claude_desktop_config.json', home / '.codex' / 'config.toml', home / '.claude' / 'settings.json']


def fingerprints():
    return {p.name + ' (' + p.parent.name + ')': (hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else 'absent') for p in real_configs()}


def session_unlocked():
    """The night part reads the screen and sends real input: a locked session gives black captures and lost clicks."""
    u32 = ctypes.WinDLL('user32', use_last_error=True)
    u32.OpenInputDesktop.restype = W.HANDLE
    h = u32.OpenInputDesktop(0, False, 0x0100)   # DESKTOP_SWITCHDESKTOP
    if not h:
        return False
    try:
        buf = ctypes.create_unicode_buffer(256); n = W.DWORD(0)
        ok = u32.GetUserObjectInformationW(W.HANDLE(h), 2, buf, ctypes.sizeof(buf), ctypes.byref(n))   # UOI_NAME
        return bool(ok) and buf.value.lower() == 'default'
    finally:
        u32.CloseDesktop(W.HANDLE(h))


# ---------------------------------------------------------------- the pause between two suites
PAUSE = OUT / 'PAUSE'


def pause_stamp():
    '''When the pause was asked (the PAUSE file's time), None when there is none.'''
    try:
        return PAUSE.stat().st_mtime
    except OSError:
        return None


def wait_pause(pauses):
    '''Between two suites: waits while the PAUSE file is there. Returns the time the pause was asked at, or None.'''
    asked = pause_stamp()
    if asked is None:
        return None
    t0 = time.time()
    print(f"paused   (build/annotation-general/PAUSE, asked at {time.strftime('%H:%M:%S', time.localtime(asked))}): the next suite waits", flush=True)
    while pause_stamp() is not None:
        time.sleep(3)
    pauses.append({'askedAt': time.strftime('%H:%M:%S', time.localtime(asked)), 'seconds': round(time.time() - t0)})
    print(f"resumed  after {round(time.time() - t0)} s", flush=True)
    return asked


# ---------------------------------------------------------------- one suite
def command_of(suite, part):
    cmd = suite['cmd'] if part == 'day' or not suite.get('nightCmd') else suite['nightCmd']
    out = []
    for a in cmd:
        a = a.replace('{build}', BUILD_NAME)
        out.append(PY if a == '{python}' else a)
    return (PS + out[1:]) if out and out[0] == '{powershell}' else out


def run_suite(suite, part, env, logs):
    """Runs one suite, once, in its own Job Object. Never retried."""
    cmd = command_of(suite, part)
    log = logs / (suite['id'] + '.log')
    job = Job(); started = time.time(); strays = set(); foreign = set()
    # where a browser.exe of the tests may live: this repository's build/ folder, or a copy the suite made under %TEMP%
    allowed = [str((ROOT / 'build').resolve()).lower() + os.sep] + [str(pathlib.Path(os.environ[v]).resolve()).lower() + os.sep for v in ('TEMP', 'TMP') if os.environ.get(v)]
    result = {'id': suite['id'], 'title': suite.get('title', suite['id']), 'cmd': ' '.join(cmd).replace(PY, 'python'), 'log': str(log.relative_to(ROOT)).replace(os.sep, '/')}
    with open(log, 'wb') as out:
        try:
            p = subprocess.Popen(cmd, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env, creationflags=CREATE_SUSPENDED)
        except OSError as e:
            job.close(); result.update(status='fail', why=f'could not start: {e}', seconds=0, strays=[], objectives=[]); return result
        try:
            job.add(p)
        except OSError as e:
            p.kill(); job.close(); result.update(status='fail', why=f'could not be put in its Job Object: {e}', seconds=0, strays=[], objectives=[]); return result
        limit = suite['timeout']; status = None
        while True:
            try:
                code = p.wait(timeout=1.0)
                status = 'pass' if code == 0 else 'fail'; result['exit'] = code
                break
            except subprocess.TimeoutExpired:
                pass
            for pid in job.pids():   # rule 8: which browser is it?
                image = image_of(pid)
                if image and os.path.basename(image).lower() == 'browser.exe' and not any(image.lower().startswith(a) for a in allowed):
                    foreign.add(image)
            if foreign:
                status = 'fail'; result['why'] = 'a browser.exe that is not a test build (neither under build/ nor under %TEMP%) was started'; job.kill(); break
            if time.time() - started > limit:
                status = 'timeout'; result['why'] = f'not finished after {limit} s'; job.kill(); break
        # what the suite left behind (up to 10 s to end by itself), then nothing survives
        deadline = time.time() + 10
        while time.time() < deadline and [x for x in job.pids() if x != p.pid]:
            time.sleep(0.5)
        for pid in job.pids():
            if pid != p.pid:
                strays.add(os.path.basename(image_of(pid)) or f'pid {pid}')
        job.kill(); job.close()
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass
    if strays and status == 'pass':
        status = 'fail'; result['why'] = 'processes left running after the suite: ' + ', '.join(sorted(strays))
    result['strays'] = sorted(strays)
    text = log.read_text(encoding='utf-8', errors='replace')
    result['objectives'] = [{'id': m.group(1), 'reached': m.group(2) == 'atteint', 'label': m.group(3).strip(), 'value': m.group(4).strip()[:300]}
                            for m in re.finditer(r'^OBJECTIF (\S+) (atteint|manque) \| (.*?) \| (.*)$', text, re.M)]
    # the #125 tutorial opened in a test browser of this suite: its veil covers the page, the suite measured the veil (rule 8).
    # Every prototype.log written under build/ since the suite started is read; a suite of the tutorial itself says `tutorial: true`.
    if not suite.get('tutorial'):
        opened = []
        for lg in (ROOT / 'build').glob('*/prototype.log'):
            try:
                if lg.stat().st_mtime >= started - 2 and 'ANNOTATE onboarding opened' in lg.read_text(encoding='utf-8', errors='replace'):
                    opened.append(lg.parent.name)
            except OSError:
                pass
        if opened:
            status = 'fail'; result['why'] = ('le tutoriel du mode annotation (#125) s\'est ouvert dans le profil de test (' + ', '.join(sorted(opened)) +
                                              ') : son voile couvre la page, cette suite n\'a pas mesuré le produit')
    marker = suite.get('skipMarker')   # a suite that says it skipped something is not a full run of it
    if marker and part == 'night' and re.search(marker, text):
        status = 'fail'; result['why'] = 'the suite says it skipped a test: ' + marker
    if part == 'day' and suite.get('daySkips'):
        result['daySkips'] = suite['daySkips']
    if status == 'fail' and not result.get('why'):   # what failed, in the report itself (the suites' own FAIL lines)
        lines = [l.strip()[:160] for l in text.splitlines() if re.match(r'(FAIL|FAILED|!!) ', l.strip())][:3]
        result['why'] = f"code de sortie {result.get('exit')}" + (' — ' + ' · '.join(lines) if lines else '')
    result['status'] = status; result['seconds'] = round(time.time() - started)
    result['tail'] = [l for l in text.splitlines() if l.strip()][-3:]
    return result


# ---------------------------------------------------------------- the report
def coverage_view(coverage, results, part, manifest):
    """Per inventory line: proved in this run / waits for the night part / failed / planned gap."""
    by = {r['id']: r for r in results}
    parts = {s['id']: s['part'] for s in manifest['suites']}
    view = {'proved': [], 'night': [], 'failed': [], 'pending': [], 'gaps': {}}
    for lid, line in coverage['lines'].items():
        tests = line.get('tests') or []
        if not tests:
            view['gaps'].setdefault(str(line['gap']['lot']), []).append(lid); continue
        ran = [by[t['suite']] for t in tests if t['suite'] in by]
        if any(r['status'] == 'pass' for r in ran):
            view['proved'].append(lid)
        elif ran and all(r['status'] == 'pending' for r in ran):
            view['pending'].append(lid)   # its only suite waits for its branch to be merged
        elif ran:
            view['failed'].append(lid)
        elif part == 'day' and all(parts.get(t['suite']) == 'night' for t in tests):
            view['night'].append(lid)
        else:
            view['failed'].append(lid)   # its suite did not run in a part where it should have
    return view


def write_report(folder, data):
    (folder / 'report.json').write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')
    idn = data['identity']; L = []
    L += ['# Test général du mode annotation — ' + ('partie sans fenêtre (journée)' if data['part'] == 'day' else 'passage complet (nuit)'), '',
          f"**Verdict : {data['verdict']}**", '', data['verdictWhy'], '',
          f"- Commit `{idn['commit'][:12]}` (version {idn['version']}, branche `{idn['branch']}`), arbre de travail " +
          ('propre' if not idn['dirty'] else f"**modifié** ({len(idn['dirty'])} fichier(s) : non valable pour une sortie)"),
          f"- Début {data['started']}, durée {data['seconds'] // 60} min {data['seconds'] % 60} s",
          f"- Valable pour une sortie : {'**oui**' if data['releaseValid'] else 'non'}", '']
    if data.get('only'):
        L += [f"Passage filtré (`--only {','.join(data['only'])}`) : il ne vaut pas pour le test général.", '']
    L += ['## Suites', '', '| Suite | Résultat | Durée | Journal | Remarque |', '|---|---|---|---|---|']
    for r in data['suites']:
        mark = {'pass': 'réussi', 'fail': '**échec**', 'timeout': '**délai dépassé**', 'not run': '**pas lancé**', 'pending': 'en attente de fusion'}[r['status']]
        note = r.get('why', '') or ('; '.join(r.get('daySkips', [])) and 'en journée, sauté : ' + '; '.join(r.get('daySkips', [])))
        L.append(f"| {r['title']} | {mark} | {r.get('seconds', 0)} s | `{r.get('log', '')}` | {note} |")
    if data.get('pauses') or data.get('setAside'):
        L += ['', '## Pauses (écran rendu au propriétaire)', '']
        for q in data.get('pauses', []):
            L.append(f"- Pause demandée à {q['askedAt']}, {q['seconds'] // 60} min {q['seconds'] % 60} s d'attente entre deux suites (hors délais).")
        for r in data.get('setAside', []):
            L.append(f"- `{r['id']}` tournait quand la pause a été demandée : premier passage écarté ({r['status']}, `{r.get('log', '')}`), rejoué après la pause — c'est le second qui compte.")
    L += ['', '## Objectifs (mesurés, jamais bloquants — décision 5 du 24/11)', '']
    if not data['objectives']:
        L.append('Aucune mesure dans cette partie (les objectifs se mesurent en vraie fenêtre).')
    for oid, o in data['objectives'].items():
        L.append(f"- `{oid}` ({o['value']}) : atteint {o['reached']} fois sur {o['total']}" + ('' if not o['missed'] else ' ; manqué : ' + ' · '.join(o['missed'][:8])))
    cv = data['coverage']
    L += ['', '## Couverture de l\'inventaire', '',
          f"- Lignes prouvées par ce passage : {len(cv['proved'])}",
          f"- Lignes dont le test a échoué ou n'a pas tourné : {len(cv['failed'])}" + ('' if not cv['failed'] else ' — ' + ', '.join(cv['failed']))]
    if data['part'] == 'day':
        L.append(f"- Lignes vérifiées seulement la nuit (vraie fenêtre) : {len(cv['night'])}")
    if cv.get('pending'):
        L.append(f"- En attente de fusion (suite pas encore dans ce commit) : {', '.join(cv['pending'])}")
    for lot, ids in sorted(cv['gaps'].items()):
        L.append(f"- Sans test, prévu au lot {lot} : {', '.join(ids)}")
    L += ['', '## Sécurité', '',
          '- Configurations réelles de Claude / Codex : ' + ('inchangées' if data['fingerprintsSame'] else '**MODIFIÉES pendant le passage**'),
          '- Processus restés ouverts après une suite : ' + (', '.join(sorted({s for r in data['suites'] for s in r.get('strays', [])})) or 'aucun'), '']
    L += ['## Fichiers de test changés depuis le dernier vert', '']
    ch = data['changedTests']
    L += [ch['note']] + [f'- `{f}`' for f in ch['files'][:80]]
    L += ['', '## Rejouer', '', f"`{data['command']}`", '']
    (folder / 'report.md').write_text('\n'.join(L) + '\n', encoding='utf-8')


def changed_tests(folder, idn):
    last = folder / 'last-green.json'
    if not last.exists():
        return {'note': 'Aucun passage vert enregistré sur ce poste pour cette partie : rien à comparer.', 'files': []}
    prev = json.loads(last.read_text(encoding='utf-8')).get('commit', '')
    files = [f for f in git('diff', '--name-only', f'{prev}..HEAD', '--', 'tests', 'test.ps1', 'test-*.ps1').splitlines() if f]
    return {'note': f"Depuis le dernier vert (`{prev[:12]}`) : {len(files)} fichier(s)." , 'files': files}


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--day', action='store_true'); ap.add_argument('--night', action='store_true')
    ap.add_argument('--build', action='store_true'); ap.add_argument('--only', default='')
    ap.add_argument('--list', action='store_true'); ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    try:
        manifest, coverage = load()
    except (OSError, ValueError) as e:
        print('manifest / coverage unreadable:', e); return 2
    bad = check_static(manifest, coverage)
    if args.list:
        for s in manifest['suites']:
            print(f"{s['part']:5} {s['id']:28} {s['timeout']:>6} s  {' '.join(s['cmd'])}")
        return 0
    if args.check or bad:
        for b in bad:
            print('FAIL', b)
        print(f"{len(manifest['suites'])} suites, {len(coverage['lines'])} inventory lines, " + ('nothing wrong' if not bad else f'{len(bad)} thing(s) wrong'))
        return 2 if bad else 0
    if args.day == args.night:
        print('choose --day (windowless) or --night (everything, real windows)'); return 2
    part = 'night' if args.night else 'day'
    only = [x for x in args.only.split(',') if x]
    unknown = [x for x in only if x not in [s['id'] for s in manifest['suites']]]
    if unknown:
        print('unknown suite(s):', ', '.join(unknown)); return 2
    if not (ROOT / 'tools' / 'w64devkit' / 'bin' / 'g++.exe').exists():
        print('tools/w64devkit is missing (setup.ps1): the suites cannot compile'); return 2
    if part == 'night' and not session_unlocked():
        print('the Windows session is locked: the real-window part reads the screen and sends real input — not started'); return 2

    folder = OUT / part; logs = folder / 'logs'; logs.mkdir(parents=True, exist_ok=True)
    idn = identity(); started = time.time(); before = fingerprints()
    env = dict(os.environ); env.pop('BROWSER_ANNOTATIONS_TEST', None)   # each suite sets its own guards (as tests/chat/run_suites.py)
    env['CHAT_QA_BUILD'] = BUILD_NAME; env['PYTHONIOENCODING'] = 'utf-8'
    outer = Job()   # rule 8: this run and everything it starts die together
    k32.AssignProcessToJobObject(outer.h, k32.GetCurrentProcess())

    suites = [s for s in manifest['suites'] if part == 'night' or s['part'] == 'day']
    results = []
    if part == 'night':
        build = {'id': 'build', 'title': 'Build neuf du commit exact', 'part': 'night', 'timeout': 3600,
                 'cmd': ['{python}', 'tools/release/build.py', '--out', 'build/' + BUILD_NAME, '--channel', 'dev', '--preview']}
        version = ROOT / 'build' / BUILD_NAME / 'version.json'
        if args.build:
            r = run_suite(build, part, env, logs); results.append(r)
            print(f"{r['status']:8} {r['id']} ({r['seconds']} s)", flush=True)
        have = json.loads(version.read_text(encoding='utf-8')).get('commit', '') if version.exists() else ''
        if have != idn['commit']:
            results.append({'id': 'build-identity', 'title': 'Le build est celui du commit testé', 'status': 'fail', 'seconds': 0,
                            'why': f"build/{BUILD_NAME} is of commit {have[:12] or '(none)'}, not {idn['commit'][:12]}: run with --build"})
    blocked = any(r['status'] != 'pass' for r in results)   # no build of this commit: the real-window suites would test something else
    pauses = []; set_aside = []
    for s in suites:
        if only and s['id'] not in only:
            continue
        wait_pause(pauses)
        script = next((a for a in s.get('cmd', []) if a.endswith(('.py', '.ps1'))), None)
        if s.get('pendingMerge') and script and not (ROOT / script).exists():   # its script is on a branch not merged yet
            results.append({'id': s['id'], 'title': s.get('title', s['id']), 'status': 'pending', 'seconds': 0,
                            'why': 'en attente de fusion : ' + script + ' arrive avec ' + s['pendingMerge']})
            print(f"pending  {s['id']} (waits for {s['pendingMerge']})", flush=True); continue
        if blocked and s['part'] == 'night':
            results.append({'id': s['id'], 'title': s.get('title', s['id']), 'status': 'not run', 'seconds': 0, 'why': 'no build of this commit'}); continue
        began = time.time()
        r = run_suite(s, part, env, logs)
        asked = pause_stamp()
        if asked is not None and asked >= began - 1:   # the pause was asked while this suite ran: the owner's window may have touched it
            first = dict(r); first['setAside'] = 'à rejouer (fenêtre du propriétaire)'; set_aside.append(first)
            keep = logs / (s['id'] + '.premier-passage.log')
            try:
                keep.write_bytes((logs / (s['id'] + '.log')).read_bytes()); first['log'] = str(keep.relative_to(ROOT)).replace(os.sep, '/')
            except OSError:
                pass
            print(f"set aside {r['id']} ({r['status']}): the pause was asked while it ran — run again after it", flush=True)
            wait_pause(pauses)
            r = run_suite(s, part, env, logs); r['rerunAfterPause'] = True
        results.append(r)
        print(f"{r['status']:8} {r['id']} ({r['seconds']} s)" + (' — ' + r['why'] if r.get('why') else ''), flush=True)
    ran = {r['id'] for r in results}
    if not only:
        for s in suites:   # rule 2: whatever did not run is said, and is red
            if s['id'] not in ran:
                results.append({'id': s['id'], 'title': s.get('title', s['id']), 'status': 'not run', 'seconds': 0, 'why': 'not reached'})

    # objectives (rule 6): only the ones the manifest names
    named = {t['id']: t for t in manifest.get('thresholds', []) if t.get('kind') == 'objectif'}
    objectives = {}; rogue = []
    for r in results:
        for o in r.get('objectives', []):
            if o['id'] not in named:
                rogue.append(f"{r['id']}: {o['id']}"); continue
            e = objectives.setdefault(o['id'], {'value': named[o['id']].get('value', ''), 'reached': 0, 'total': 0, 'missed': []})
            e['total'] += 1; e['reached'] += o['reached']
            if not o['reached']:
                e['missed'].append(f"{o['label']} ({o['value'][:80]})")
    after = fingerprints(); same = after == before
    cv = coverage_view(coverage, results, part, manifest)
    waiting = [r for r in results if r['status'] == 'pending']   # not run: their script is not in this commit — never green, never a failure of this commit
    failed = [r for r in results if r['status'] not in ('pass', 'pending')]
    reasons = []
    if failed:
        reasons.append(f"{len(failed)} suite(s) en échec, hors délai ou pas lancée(s) : " + ', '.join(r['id'] for r in failed))
    if cv['failed'] and not only:
        reasons.append(f"{len(cv['failed'])} ligne(s) de l'inventaire sans preuve dans ce passage")
    if rogue:
        reasons.append('objectif(s) non déclaré(s) dans le manifeste : ' + ', '.join(sorted(set(rogue))))
    if not same:
        reasons.append('une configuration réelle de Claude / Codex a changé pendant le passage')
    green = not reasons
    if only:
        verdict, why, code = 'PARTIEL — non valable', 'Passage filtré par --only : il sert à chercher un défaut, pas à valider. ' + (' ; '.join(reasons) if reasons else 'Les suites lancées ont réussi.'), 3
    elif green:
        verdict, why, code = 'VERT', ('Toute la partie sans fenêtre a tourné et réussi.' if part == 'day' else 'Toutes les suites ont tourné et réussi, en vraie fenêtre.'), 0
        if waiting:
            verdict = f'VERT, sauf {len(waiting)} suite(s) en attente de fusion'
            why += ' Pas lancée(s), leur script n\'est pas encore dans ce commit : ' + ', '.join(r['id'] for r in waiting) + '.'
    else:
        verdict, why, code = 'ROUGE', ' ; '.join(reasons) + '.', 1
    release = part == 'night' and green and not only and not idn['dirty']
    data = {'part': part, 'verdict': verdict, 'verdictWhy': why, 'releaseValid': release, 'identity': idn, 'only': only,
            'started': time.strftime('%d/%m/%Y %H:%M', time.localtime(started)), 'seconds': round(time.time() - started),
            'suites': results, 'objectives': objectives, 'coverage': cv, 'fingerprintsSame': same, 'pauses': pauses, 'setAside': set_aside,
            'changedTests': changed_tests(folder, idn),
            'command': 'python tests/annotation-general/run.py --' + part + (' --build' if args.build else '') + (' --only ' + ','.join(only) if only else '')}
    write_report(folder, data)
    if green and not only:
        (folder / 'last-green.json').write_text(json.dumps({'commit': idn['commit'], 'at': data['started']}), encoding='utf-8')
    print(f"\n{verdict} — {why}\nreport: {(folder / 'report.md').relative_to(ROOT)}")
    return code   # `outer` closes with this process: whatever is still alive in it dies with it


if __name__ == '__main__':
    sys.exit(main())
