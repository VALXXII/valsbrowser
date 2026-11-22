#!/usr/bin/env python3
"""Real-window QA of the first-run onboarding (src/onboarding.h + src/onboarding-integration.h).

  python tests/onboarding/qa.py --app build/onboarding-app [--evidence design/onboarding-evidence/real] [--only full,error,skip,english]

Each scenario runs a THROWAWAY COPY of the build (portable layout: its profile, session and log live inside the
copy), started with « --onboarding --import-sources <synthetic folder> ». Nothing of the person's is touched:
LOCALAPPDATA / APPDATA point into the run folder, the import sources are made by
tests/browser-import/make_sources.py (RFC 2606 addresses), the agent connection is redirected
(BROWSER_ANNOTATIONS_TEST / BROWSER_ANNOTATIONS_CONNECT_ROOT). Every process started here belongs to a job object
with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE (it dies with this script, even interrupted) and, at the end, anything
still running from the run folder is killed by exact path.

The window is driven with messages posted to it (clicks at Paper coordinates, keys): no global input injection.
Captures need an unlocked session; without one the checks still run and the captures are reported as missing.

Scenarios
  full     1 → 2 (name, icon, hue, saved to the registry) → 3 → 4 → 5 (bookmarks and history really written) → 6 →
           « Activer » → 7 → browser window, « Connecter un agent » opens. Shader animation, pause when minimised.
  error    Chrome « open » (its History held): 5.1 with the reason, bookmarks kept; released; « Réessayer » → 5.
  skip     Échap on the first board: straight to the browser, nothing written.
  english  the language picker switches the window at once and writes the preference.
  firstrun no switch at all: an installed layout (<LOCALAPPDATA>\\browser\\versions\\<v>\\browser.exe, data in
           <root>\\data) with no data shows the window by itself, once; a portable folder never does;
           BROWSER_NO_ONBOARDING=1 keeps an automated install out of it.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # per-monitor v2: real pixels for rectangles, captures and clicks
kernel32.OpenProcess.restype = wt.HANDLE
kernel32.CreateJobObjectW.restype = wt.HANDLE
kernel32.CreateFileW.restype = wt.HANDLE
WM_CLOSE, WM_KEYDOWN, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MOUSEMOVE, WM_SETTEXT, WM_SYSCOMMAND = 0x10, 0x100, 0x201, 0x202, 0x200, 0x0C, 0x112
VK_ESCAPE, VK_RETURN, VK_TAB = 0x1B, 0x0D, 0x09
results = []
sys.stdout.reconfigure(encoding='utf-8', errors='replace')


def check(ok, what):
    results.append((bool(ok), what))
    print(('ok   ' if ok else 'FAIL ') + what, flush=True)
    return ok


# ---- processes: one job, killed with the script ----------------------------------------------------------
class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong), ('LimitFlags', wt.DWORD),
                ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wt.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', wt.DWORD), ('SchedulingClass', wt.DWORD)]


class IO_COUNTERS(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ('r', 'w', 'o', 'rt', 'wt', 'ot')]


class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', JOBOBJECT_BASIC_LIMIT_INFORMATION), ('IoInfo', IO_COUNTERS), ('ProcessMemoryLimit', ctypes.c_size_t),
                ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


def make_job():
    job = kernel32.CreateJobObjectW(None, None)
    info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
    info.BasicLimitInformation.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not job or not kernel32.SetInformationJobObject(wt.HANDLE(job), 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise SystemExit('job object not created: refusing to start anything')
    return job


def kill_by_path(folder: Path):
    """Anything still running from the run folder (exact path prefix), whatever started it."""
    script = ("$p = '%s'; Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($p, 'OrdinalIgnoreCase') } | "
              "ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }") % str(folder).replace("'", "''")
    subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True, timeout=60)


def cpu_seconds(pid):
    h = kernel32.OpenProcess(0x1000, False, pid)
    if not h:
        return None
    c, e, k, u = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
    ok = kernel32.GetProcessTimes(wt.HANDLE(h), ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u))
    kernel32.CloseHandle(wt.HANDLE(h))
    if not ok:
        return None
    return (((k.dwHighDateTime << 32) | k.dwLowDateTime) + ((u.dwHighDateTime << 32) | u.dwLowDateTime)) / 1e7


# ---- windows -----------------------------------------------------------------------------------------
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def windows_of(pid, cls, visible=True):
    found = []

    def each(h, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(h, name, 64)
        if owner.value == pid and name.value == cls and (not visible or user32.IsWindowVisible(h)):
            found.append(h)
        return True
    user32.EnumWindows(WNDENUMPROC(each), 0)
    return found


def wait_window(pid, cls, seconds=20.0):
    end = time.time() + seconds
    while time.time() < end:
        w = windows_of(pid, cls)
        if w:
            return w[0]
        time.sleep(.05)
    return None


def wait_gone(pid, cls, seconds=10.0):
    end = time.time() + seconds
    while time.time() < end:
        if not windows_of(pid, cls, visible=False):
            return True
        time.sleep(.05)
    return False


class Ui:
    """The onboarding window, addressed in Paper coordinates (DIP)."""

    def __init__(self, hwnd):
        self.hwnd = hwnd
        dpi = user32.GetDpiForWindow(hwnd) or 96
        self.scale = dpi / 96.0

    def lp(self, x, y):
        return (int(round(y * self.scale)) << 16) | (int(round(x * self.scale)) & 0xFFFF)

    def click(self, x, y, hwnd=None):
        h = hwnd or self.hwnd
        user32.PostMessageW(h, WM_MOUSEMOVE, 0, self.lp(x, y))
        user32.PostMessageW(h, WM_LBUTTONDOWN, 1, self.lp(x, y))
        user32.PostMessageW(h, WM_LBUTTONUP, 0, self.lp(x, y))

    def key(self, vk):
        user32.PostMessageW(self.hwnd, WM_KEYDOWN, vk, 0)

    def rect(self):
        r = wt.RECT()
        user32.GetWindowRect(self.hwnd, ctypes.byref(r))
        return r.left, r.top, r.right, r.bottom

    def printed(self):
        """The window as the compositor holds it (PrintWindow, full content): no other application's overlay in it."""
        from PIL import Image
        gdi32 = ctypes.windll.gdi32
        left, top, right, bottom = self.rect()
        w, h = right - left, bottom - top
        screen = user32.GetWindowDC(self.hwnd)
        memory = gdi32.CreateCompatibleDC(wt.HDC(screen))
        bitmap = gdi32.CreateCompatibleBitmap(wt.HDC(screen), w, h)
        gdi32.SelectObject(wt.HDC(memory), wt.HANDLE(bitmap))
        ok = user32.PrintWindow(self.hwnd, wt.HDC(memory), 2)   # PW_RENDERFULLCONTENT
        header = (ctypes.c_uint32 * 10)(40, w, ctypes.c_uint32(-h).value, 1 | (32 << 16), 0, 0, 0, 0, 0, 0)
        pixels = ctypes.create_string_buffer(w * h * 4)
        gdi32.GetDIBits(wt.HDC(memory), wt.HANDLE(bitmap), 0, h, pixels, ctypes.byref(header), 0)
        gdi32.DeleteObject(wt.HANDLE(bitmap)); gdi32.DeleteDC(wt.HDC(memory)); user32.ReleaseDC(self.hwnd, wt.HDC(screen))
        if not ok:
            return None
        img = Image.frombuffer('RGBA', (w, h), pixels.raw, 'raw', 'BGRA', 0, 1).convert('RGB')
        return img if img.getextrema() != ((0, 0), (0, 0), (0, 0)) else None

    def grab(self, path: Path):
        try:
            img = self.printed()
            if img is None:   # no compositor copy: the screen itself (needs an unlocked session)
                from PIL import ImageGrab
                user32.SetForegroundWindow(self.hwnd)
                time.sleep(.12)
                img = ImageGrab.grab(bbox=self.rect(), all_screens=True)
            path.parent.mkdir(parents=True, exist_ok=True)
            img.save(path)
            return img
        except Exception as error:   # locked session, no PIL
            print('     capture missing (%s): %s' % (path.name, error), flush=True)
            return None

    def activate(self):
        """Makes the window the active one without injecting input: a restored window is activated by the system."""
        user32.ShowWindow(self.hwnd, 6); time.sleep(.35)
        user32.ShowWindow(self.hwnd, 9); time.sleep(.45)
        return user32.GetForegroundWindow() == self.hwnd


def differ(a, b, box):
    """Mean absolute difference of a DIP box between two captures (None when a capture is missing)."""
    if a is None or b is None:
        return None
    from PIL import ImageChops, ImageStat
    s = a.size[0] / 720.0
    area = tuple(int(v * s) for v in box)
    return sum(ImageStat.Stat(ImageChops.difference(a.crop(area).convert('RGB'), b.crop(area).convert('RGB'))).mean) / 3


# ---- one run -----------------------------------------------------------------------------------------
class Run:
    def __init__(self, app: Path, work: Path, name: str, evidence: Path, language=None):
        self.name, self.evidence = name, evidence
        self.dir = work / name
        if self.dir.exists():
            shutil.rmtree(self.dir)
        self.app = self.dir / 'app'
        shutil.copytree(app, self.app, ignore=shutil.ignore_patterns('profile', 'profiles', 'profiles.txt', 'session*', 'settings', '*.log', 'windows', 'ui-language', 'downloads', 'bookmarks*', 'history*'))
        self.sources = self.dir / 'sources'
        subprocess.run([sys.executable, '-I', str(ROOT / 'tests/browser-import/make_sources.py'), str(self.sources)], check=True, capture_output=True)
        # A second Chrome profile (the profile select of board 3) and the boards' logo for the registered Chrome.
        second = self.sources / 'Local/Google/Chrome/User Data/Profile 1'
        second.mkdir(parents=True)
        shutil.copyfile(self.sources / 'Local/Google/Chrome/User Data/Default/Bookmarks', second / 'Bookmarks')
        try:
            from PIL import Image
            ico = self.dir / 'source-logo.ico'
            Image.open(ROOT / 'design/onboarding-evidence/fixtures/source-logo.png').save(ico, sizes=[(128, 128), (96, 96), (48, 48), (32, 32)])
            listing = self.sources / 'startmenuinternet.txt'
            text = listing.read_text(encoding='utf-8').splitlines()
            text[0] = '|'.join(text[0].split('|')[:3] + [str(ico)])
            listing.write_text('\n'.join(text) + '\n', encoding='utf-8')
        except Exception as error:
            print('     logo fixture not written:', error)
        if language:
            (self.app / 'ui-language').write_text('browser-ui-language 1\nlanguage=%s\n' % language, encoding='ascii', newline='\n')
        self.env = dict(os.environ)
        for key in ('LOCALAPPDATA', 'APPDATA'):
            (self.dir / key).mkdir(exist_ok=True)
            self.env[key] = str(self.dir / key)
        self.env['BROWSER_ANNOTATIONS_TEST'] = '1'
        (self.dir / 'connect').mkdir(exist_ok=True)
        self.env['BROWSER_ANNOTATIONS_CONNECT_ROOT'] = str(self.dir / 'connect')
        self.env.pop('BROWSER_NO_ONBOARDING', None)
        self.job, self.proc, self.ui = make_job(), None, None

    def launch(self, exe: Path, args, env=None):
        """Starts `exe` inside the job (suspended until it is in)."""
        self.proc = subprocess.Popen([str(exe), *args], cwd=str(exe.parent), env=env or self.env, creationflags=0x00000004)
        handle = kernel32.OpenProcess(0x1F0FFF, False, self.proc.pid)
        if not handle or not kernel32.AssignProcessToJobObject(wt.HANDLE(self.job), wt.HANDLE(handle)):
            self.proc.kill()
            raise SystemExit('process not placed in the job: refusing to run it')
        ctypes.windll.ntdll.NtResumeProcess(wt.HANDLE(handle))
        kernel32.CloseHandle(wt.HANDLE(handle))
        return self.proc

    def close_browser(self):
        for h in windows_of(self.proc.pid, 'LisiereBrowser', visible=False):
            user32.PostMessageW(h, WM_CLOSE, 0, 0)
        try:
            self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()

    def start(self, *extra):
        exe = self.app / 'browser.exe'
        self.proc = subprocess.Popen([str(exe), '--onboarding', '--import-sources', str(self.sources), *extra], cwd=str(self.app), env=self.env,
                                     creationflags=0x00000004)   # CREATE_SUSPENDED: in the job before it runs
        handle = kernel32.OpenProcess(0x1F0FFF, False, self.proc.pid)
        if not handle or not kernel32.AssignProcessToJobObject(wt.HANDLE(self.job), wt.HANDLE(handle)):
            self.proc.kill()
            raise SystemExit('process not placed in the job: refusing to run it')
        ctypes.windll.ntdll.NtResumeProcess(wt.HANDLE(handle))
        kernel32.CloseHandle(wt.HANDLE(handle))
        hwnd = wait_window(self.proc.pid, 'LisiereOnboarding')
        if not check(hwnd is not None, '%s: the onboarding window opens with --onboarding' % self.name):
            return False
        self.ui = Ui(hwnd)
        time.sleep(.7)
        return True

    def shot(self, label):
        return self.ui.grab(self.evidence / ('%s-%s.png' % (self.name, label)))

    def log(self):
        path = self.app / 'prototype.log'
        for encoding in ('utf-8', 'utf-16'):
            try:
                return path.read_text(encoding=encoding)
            except Exception:
                continue
        return ''

    def browser_window(self, seconds=30.0):
        return wait_window(self.proc.pid, 'LisiereBrowser', seconds)

    def stop(self):
        try:
            for h in windows_of(self.proc.pid, 'LisiereBrowser', visible=False) + windows_of(self.proc.pid, 'LisiereOnboarding', visible=False):
                user32.PostMessageW(h, WM_CLOSE, 0, 0)
            try:
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                pass
        finally:
            kernel32.CloseHandle(wt.HANDLE(self.job))   # KILL_ON_JOB_CLOSE: everything it started dies here
            kill_by_path(self.dir)


def profile_file(run, name):
    return run.app / name   # the default profile keeps the legacy layout: its files sit at the data root


def scenario_full(app, work, evidence):
    run = Run(app, work, 'full', evidence)
    try:
        if not run.start():
            return
        ui = run.ui
        left, top, right, bottom = ui.rect()
        check(abs((right - left) - round(720 * ui.scale)) <= 1 and abs((bottom - top) - round(580 * ui.scale)) <= 1, 'full: window is 720 x 580 DIP at %d %% scale' % round(ui.scale * 100))
        a = run.shot('1')
        time.sleep(.45)
        b = run.shot('1-later')
        moved = differ(a, b, (190, 90, 530, 400))
        if moved is not None:
            check(moved > .2, 'full: the halftone logo is animated (frames 0.45 s apart differ by %.2f / 255)' % moved)
            still = differ(a, b, (20, 520, 700, 570))
            check(still is not None and still < .5, 'full: nothing else moves on the first board (%.2f / 255)' % (still or 0))
        before = cpu_seconds(run.proc.pid); time.sleep(2.0); after = cpu_seconds(run.proc.pid)
        if before is not None and after is not None:
            print('     CPU while the logo loops: %.1f %% of one core' % ((after - before) / 2.0 * 100), flush=True)
            check((after - before) / 2.0 < .30, 'full: the animated board (dots over the whole window) stays light on the CPU')
        user32.ShowWindow(ui.hwnd, 6); time.sleep(.6)   # SW_MINIMIZE
        before = cpu_seconds(run.proc.pid); time.sleep(1.5); after = cpu_seconds(run.proc.pid)
        if before is not None and after is not None:
            check((after - before) / 1.5 < .02, 'full: minimised, the animation is paused (%.1f %% of one core)' % ((after - before) / 1.5 * 100))
        user32.ShowWindow(ui.hwnd, 9); time.sleep(.5)    # SW_RESTORE
        ui.click(649, 546); time.sleep(.6)               # « Commencer »
        run.shot('2')
        edits = windows_child(ui.hwnd)
        ui.click(60, 481); time.sleep(.3)                # the name field
        edits = windows_child(ui.hwnd)
        check(len(edits) == 1 and user32.IsWindowVisible(edits[0]), 'full: clicking the name shows the native field')
        if edits:
            user32.SendMessageW(edits[0], WM_SETTEXT, 0, ctypes.c_wchar_p('QA Léa'))
        time.sleep(.2)
        run.shot('2-name-focused')
        ui.click(183, 481); time.sleep(.3)               # the second icon: house
        ui.click(371 + 2 + 278 * 300 / 359.0, 457); time.sleep(.3)   # Teinte: 300 degrees
        c = run.shot('2-edited')
        ui.click(656, 546); time.sleep(.8)               # « Continuer »
        run.shot('3')
        registry = (run.app / 'profiles.txt').read_text(encoding='utf-8', errors='replace') if (run.app / 'profiles.txt').exists() else ''
        fields = next((line[2:].split('|') for line in registry.splitlines() if line.startswith('p=default|')), [])
        check(len(fields) >= 6 and fields[1] == 'house' and abs(int(fields[2]) - 300) <= 2 and fields[4] == '0' and fields[5] == 'QA Léa', 'full: « Continuer » wrote name, icon and hue to the profile registry (%s)' % '|'.join(fields))
        ui.click(616, 546); time.sleep(.25)              # « Importer mes données »
        run.shot('4')
        time.sleep(2.2)
        run.shot('5')
        bookmarks = profile_file(run, 'bookmarks'); history = profile_file(run, 'history')
        btext = bookmarks.read_text(encoding='utf-8', errors='replace') if bookmarks.exists() else ''
        htext = history.read_text(encoding='utf-8', errors='replace') if history.exists() else ''
        check('home.example' in btext and 'docs.example' in btext, 'full: the synthetic Chrome bookmarks are in the profile')
        check('news.example' in htext and 'hidden.example' not in htext, 'full: the synthetic history is in the profile (hidden rows left out)')
        ui.click(360, 546); time.sleep(.7)               # « Continuer »
        d = run.shot('6')
        time.sleep(.6)
        e = run.shot('6-later')
        moved = differ(d, e, (0, 60, 720, 150))
        if moved is not None:
            check(moved > .1, 'full: the annotation gradient is animated (%.2f / 255)' % moved)
        ui.click(667, 547); time.sleep(.45)              # « Activer »
        run.shot('7')
        main = run.browser_window()
        check(main is not None, 'full: after the closing logo the browser window opens')
        check(wait_gone(run.proc.pid, 'LisiereOnboarding'), 'full: the onboarding window is gone')
        end = time.time() + 25
        while time.time() < end and 'ONBOARDING opens the agent connection' not in run.log():
            time.sleep(.25)
        time.sleep(1.5)
        if main:
            Ui(main).grab(evidence / 'full-browser.png')
        log = run.log()
        check('ONBOARDING shown (requested)' in log and 'ONBOARDING ended: completed' in log and 'annotation asked' in log, 'full: log: shown, completed, annotation asked')
        check('ONBOARDING opens the agent connection' in log, 'full: « Activer » opened the agent connection in the browser')
        check('home.example' not in log and 'QA L' not in log, 'full: the log carries no address and no name')
    finally:
        run.stop()


def windows_child(parent):
    found = []

    def each(h, _):
        name = ctypes.create_unicode_buffer(32)
        user32.GetClassNameW(h, name, 32)
        if name.value.lower() == 'edit':
            found.append(h)
        return True
    user32.EnumChildWindows(parent, WNDENUMPROC(each), 0)
    return found


def scenario_error(app, work, evidence):
    run = Run(app, work, 'error', evidence)
    held = None
    try:
        if not run.start():
            return
        ui = run.ui
        ui.click(649, 546); time.sleep(.5)
        ui.click(656, 546); time.sleep(.8)
        # A running Chrome keeps History open for writing.
        path = run.sources / 'Local/Google/Chrome/User Data/Default/History'
        held = kernel32.CreateFileW(str(path), 0xC0000000, 3, None, 3, 0, None)
        check(held and held != wt.HANDLE(-1).value, 'error: the source History is held as by a running Chrome')
        ui.click(616, 546); time.sleep(2.4)
        run.shot('5.1')
        log = run.log()
        check('ONBOARDING history read failed' in log and 'ONBOARDING bookmarks saved' in log, 'error: history refused (source in use), bookmarks saved')
        bookmarks = profile_file(run, 'bookmarks')
        check(bookmarks.exists() and 'home.example' in bookmarks.read_text(encoding='utf-8', errors='replace'), 'error: the bookmarks that could be read are kept')
        kernel32.CloseHandle(wt.HANDLE(held)); held = None
        ui.click(655, 546); time.sleep(2.4)              # « Réessayer »
        run.shot('5-after-retry')
        history = profile_file(run, 'history')
        check(history.exists() and 'news.example' in history.read_text(encoding='utf-8', errors='replace'), 'error: « Réessayer » imported the history once Chrome let go')
        check(run.log().count('ONBOARDING bookmarks saved') == 1, 'error: the retry did not import the bookmarks a second time')
        ui.click(360, 546); time.sleep(.7)               # « Continuer » → board 6
        ui.click(80, 547); time.sleep(.3)                # « Pas intéressé(e) »
        main = run.browser_window()
        check(main is not None, 'error: the browser opens after « Pas intéressé(e) »')
        time.sleep(2.5)
        check('ONBOARDING opens the agent connection' not in run.log() and 'annotation asked' not in run.log(), 'error: no agent connection proposed after « Pas intéressé(e) »')
    finally:
        if held:
            kernel32.CloseHandle(wt.HANDLE(held))
        run.stop()


def uia_names(hwnd):
    """What a screen reader finds in the window: « type|name|automation id » per element (UI Automation client)."""
    script = ("Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes; $e=[System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]%d); "
              "$e.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition) | "
              "ForEach-Object { $_.Current.ControlType.ProgrammaticName + '|' + $_.Current.Name + '|' + $_.Current.AutomationId }") % hwnd
    r = subprocess.run(['powershell', '-NoProfile', '-Command', '[Console]::OutputEncoding=[Text.Encoding]::UTF8; ' + script], capture_output=True, timeout=60)
    return r.stdout.decode('utf-8', errors='replace').splitlines()


def scenario_skip(app, work, evidence):
    run = Run(app, work, 'skip', evidence)
    try:
        if not run.start():
            return
        names = uia_names(run.ui.hwnd)
        def has(kind, name, automation):
            return any(line == 'ControlType.%s|%s|%s' % (kind, name, automation) for line in names)
        check(has('Button', 'Commencer', 'OnboardingPrimary') and has('Button', 'Passer et naviguer', 'OnboardingSkip') and has('Button', 'Fermer', 'OnboardingClose') and has('Button', 'Langue', 'OnboardingLanguage'),
              'skip: UI Automation exposes the four controls of the first board by name (%d elements)' % len(names))
        check(any(line.startswith('ControlType.Group|Bienvenue dans Browser|Onboarding') for line in names), 'skip: UI Automation: the board is a group named by its title')
        started = time.time()
        run.ui.key(VK_ESCAPE)
        check(wait_gone(run.proc.pid, 'LisiereOnboarding', 3), 'skip: Échap closes the window')
        main = run.browser_window()
        check(main is not None, 'skip: the browser opens (%.1f s after Échap)' % (time.time() - started))
        time.sleep(1.5)
        registry = (run.app / 'profiles.txt').read_text(encoding='utf-8', errors='replace') if (run.app / 'profiles.txt').exists() else ''
        check('Personnel' in registry and 'heart' not in registry, 'skip: the default profile is untouched')
        check(not profile_file(run, 'bookmarks').exists() or 'home.example' not in profile_file(run, 'bookmarks').read_text(encoding='utf-8', errors='replace'), 'skip: nothing imported')
        check('ONBOARDING ended: skipped' in run.log(), 'skip: log: skipped')
    finally:
        run.stop()


def scenario_english(app, work, evidence):
    run = Run(app, work, 'english', evidence)
    try:
        if not run.start():
            return
        ui = run.ui
        active = ui.activate()   # a menu closes as soon as it is not the active window
        ui.click(50, 25); time.sleep(.5)                 # the language button
        run.shot('1-language-menu')
        menus = windows_of(run.proc.pid, 'LisiereContextMenu')
        if not menus and not active:
            print('     SKIP english: this session cannot make the window active (someone else has the foreground): the menu closed at once; the switch itself is covered by onboarding_render_test', flush=True)
            return
        check(len(menus) == 1, 'english: the language button opens a menu')
        if menus:
            Ui(menus[0]).grab(run.evidence / 'english-1-menu-popup.png')
            ui.click(60, 4 + 32 + 16, menus[0]); time.sleep(.6)   # second row: English
        run.shot('1-english')
        preference = run.app / 'ui-language'
        check(preference.exists() and 'language=en' in preference.read_text(encoding='ascii', errors='replace'), 'english: the preference is written (language=en)')
        title = ctypes.create_unicode_buffer(128)
        user32.GetWindowTextW(ui.hwnd, title, 128)
        check(title.value == 'Welcome to Browser', 'english: the window switched at once (title « %s »)' % title.value)
        ui.click(649, 546); time.sleep(.6)
        run.shot('2-english')
        ui.key(VK_ESCAPE)
        check(run.browser_window() is not None, 'english: the browser opens')
        time.sleep(1.0)
        check('UI LANGUAGE fr' in run.log() and 'ONBOARDING language en' in run.log(), 'english: log: started in French, switched to English')
    finally:
        run.stop()


def scenario_firstrun(app, work, evidence):
    run = Run(app, work, 'firstrun', evidence)
    try:
        # Portable folder, no switch: the browser, never the window.
        run.launch(run.app / 'browser.exe', [])
        main = run.browser_window()
        check(main is not None and not windows_of(run.proc.pid, 'LisiereOnboarding', visible=False), 'firstrun: a portable folder starts the browser directly')
        run.close_browser()
        # Installed layout, fresh: the window shows by itself.
        version = next((line.split('"')[3] for line in (run.app / 'version.json').read_text(encoding='utf-8').splitlines() if '"version"' in line), '0.3.1') if (run.app / 'version.json').exists() else '0.3.1'
        root = Path(run.env['LOCALAPPDATA']) / 'browser'
        installed = root / 'versions' / version
        shutil.copytree(run.app, installed, ignore=shutil.ignore_patterns('profile', 'profiles', 'profiles.txt', 'session*', 'settings', '*.log', 'windows', 'ui-language', 'downloads', 'bookmarks*', 'history*', 'extensions'))
        quiet = dict(run.env, BROWSER_NO_ONBOARDING='1')
        run.launch(installed / 'browser.exe', [], quiet)
        main = run.browser_window()
        check(main is not None and not windows_of(run.proc.pid, 'LisiereOnboarding', visible=False), 'firstrun: BROWSER_NO_ONBOARDING=1 keeps an automated first start out of the window')
        run.close_browser()
        check((root / 'data' / 'profiles.txt').exists(), 'firstrun: installed layout: the data lives in <root>\\data')
        for attempt in range(40):   # the engine's processes let go of the folder a moment after the window closes
            try:
                shutil.rmtree(root / 'data'); break
            except OSError:
                time.sleep(.25)
        run.launch(installed / 'browser.exe', [])
        hwnd = wait_window(run.proc.pid, 'LisiereOnboarding')
        check(hwnd is not None, 'firstrun: a fresh install shows the window by itself (no switch)')
        if hwnd:
            time.sleep(.6)
            Ui(hwnd).grab(evidence / 'firstrun-1.png')
            user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)   # the close button's path
        main = run.browser_window()
        check(main is not None, 'firstrun: closing it opens the browser')
        time.sleep(1.0)
        log = (root / 'data' / 'browser.log').read_text(encoding='utf-8', errors='replace') if (root / 'data' / 'browser.log').exists() else ''
        check('ONBOARDING shown (first run)' in log and 'ONBOARDING ended: skipped' in log, 'firstrun: log: shown (first run), skipped')
        run.close_browser()
        run.launch(installed / 'browser.exe', [])
        main = run.browser_window()
        check(main is not None and not windows_of(run.proc.pid, 'LisiereOnboarding', visible=False), 'firstrun: the second start goes straight to the browser')
        run.close_browser()
    finally:
        run.stop()


def scenario_debug(app, work, evidence):
    """« --onboarding --onboarding-debug »: the tuning panel of the 2 -> 3 transition; values kept between runs."""
    run = Run(app, work, 'debug', evidence)
    try:
        if not run.start('--onboarding-debug'):
            return
        panels = windows_of(run.proc.pid, 'LisiereOnboardingDebug')
        check(len(panels) == 1, 'debug: the tuning panel opens beside the window')
        if not panels:
            return
        panel = Ui(panels[0])
        panel.grab(evidence / 'debug-panel.png')
        tuning = run.app / 'onboarding-tuning.txt'
        panel.click(16 + 268 * (600 - 60) / 1440.0, 44 + 30); time.sleep(.3)             # « Durée du contenu »: 600 ms
        panel.click(16 + 268 * (900 - 60) / 1940.0, 44 + 4 * 42 + 30); time.sleep(.3)   # « Aperçu : durée du déplacement »: 900 ms
        text = tuning.read_text(encoding='ascii', errors='replace') if tuning.exists() else ''
        check('transition23.duration=600' in text and 'transition23.mockDuration=900' in text, 'debug: moving a slider writes the values file at once')
        panel.click(150, 44 + 10 * 42 + 8 + 16)                                          # « Rejouer 2 → 3 »
        time.sleep(.3); a = run.shot('replay-before')
        time.sleep(.55); b = run.shot('replay-during')
        time.sleep(1.2); c = run.shot('replay-after')
        moved = differ(a, b, (0, 40, 720, 420)); settled = differ(b, c, (0, 40, 720, 420))
        if moved is not None and settled is not None:
            check(moved > 1 and settled > 1, 'debug: « Rejouer 2 → 3 » shows board 2, plays the transition, ends on board 3 (%.1f, %.1f / 255)' % (moved, settled))
        panel.grab(evidence / 'debug-panel-tuned.png')
        run.ui.key(VK_ESCAPE)
        check(run.browser_window() is not None, 'debug: the browser opens after Échap')
        time.sleep(.8)
        check('ONBOARDING debug panel' in run.log() and 'ONBOARDING profile saved' not in run.log(), 'debug: log: debug panel; the replay saved nothing')
        run.close_browser()
        # Second debug run: the values are back. A normal run: no panel, default motion.
        run.launch(run.app / 'browser.exe', ['--onboarding-debug'])
        hwnd = wait_window(run.proc.pid, 'LisiereOnboarding')
        check(hwnd is not None and len(windows_of(run.proc.pid, 'LisiereOnboardingDebug')) == 1, 'debug: --onboarding-debug alone opens the window and its panel')
        time.sleep(.6)
        again = windows_of(run.proc.pid, 'LisiereOnboardingDebug')
        if again:
            Ui(again[0]).grab(evidence / 'debug-panel-second-run.png')
        check('transition23.duration=600' in tuning.read_text(encoding='ascii', errors='replace'), 'debug: the values are kept between debug runs')
        if hwnd:
            user32.PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0)
        run.browser_window(); time.sleep(.5); run.close_browser()
        run.launch(run.app / 'browser.exe', ['--onboarding'])
        hwnd = wait_window(run.proc.pid, 'LisiereOnboarding')
        time.sleep(.5)
        check(hwnd is not None and not windows_of(run.proc.pid, 'LisiereOnboardingDebug', visible=False), 'debug: a normal --onboarding run has no panel')
        if hwnd:
            user32.PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0)
        run.browser_window(); time.sleep(.5); run.close_browser()
    finally:
        run.stop()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--app', default='build/onboarding-app')
    parser.add_argument('--evidence', default='design/onboarding-evidence/real')
    parser.add_argument('--work', default='build/onboarding-qa')
    parser.add_argument('--only', default='full,error,skip,english,firstrun,debug')
    args = parser.parse_args()
    app, evidence, work = (ROOT / args.app).resolve(), (ROOT / args.evidence).resolve(), (ROOT / args.work).resolve()
    if not (app / 'browser.exe').exists():
        raise SystemExit('no build at %s (python tools/release/build.py --out %s --channel dev)' % (app, args.app))
    work.mkdir(parents=True, exist_ok=True)
    scenarios = {'full': scenario_full, 'error': scenario_error, 'skip': scenario_skip, 'english': scenario_english, 'firstrun': scenario_firstrun, 'debug': scenario_debug}
    try:
        for name in args.only.split(','):
            scenarios[name.strip()](app, work, evidence)
    finally:
        kill_by_path(work)
    failed = [w for ok, w in results if not ok]
    print('%d checks, %d failures' % (len(results), len(failed)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
