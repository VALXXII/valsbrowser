"""What the WebView2 engine reports for each failure, and which of our pages that gives — no window.

    python tests/error-pages/status_probe.py

Why: the night run of 24/11 found « connexion refusée » and the HTTPS-only page falling back to the generic page
(code ERR_FAILED). The engine reports a refused connection as WebErrorStatus UNKNOWN (0) without an HTTP status, like
ERR_FAILED or ERR_ACCESS_DENIED; only its own error document (chrome-error:) names the real error. This probe asks the
real engine (a WebView2 host whose window is never shown, fresh temporary profile, job object with KILL_ON_JOB_CLOSE),
reads that document with the production script (ErrorPages::EngineErrorCodeScript) and runs the result through
status_classify.exe, which makes main's decision with the very functions of src/error-pages.h.
Only local traffic: real refusals go to a closed port of 127.0.0.1 / localhost; the board addresses (lemond.fr,
impots.gouv.fr...) are failed locally by DevTools Fetch, exactly as tests/error-pages/window_qa.py does.
"""
from __future__ import annotations
import json, re, shutil, socket, subprocess, sys, tempfile, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from run import OUT, ROOT, TOOLS, WV2_INCLUDE, WV2_LOADER, run_in_job  # noqa: E402


def steps(closed: int) -> list[tuple[str, str | None, bool, str]]:
    """(url, DevTools failure or None for a real request, HTTPS-only upgrade attempt, the page we must show)."""
    return [
        (f'http://127.0.0.1:{closed}/', None, False, 'Refused ERR_CONNECTION_REFUSED'),
        (f'http://localhost:{closed}/', None, False, 'Refused ERR_CONNECTION_REFUSED'),
        ('http://localhost:3000/', 'ConnectionRefused', False, 'Refused ERR_CONNECTION_REFUSED'),
        ('https://lemond.fr/', 'NameNotResolved', False, 'NameNotResolved ERR_NAME_NOT_RESOLVED'),
        ('https://impots.gouv.fr/', 'TimedOut', False, 'TimedOut ERR_TIMED_OUT'),
        ('https://www.lemonde.fr/planete/', 'InternetDisconnected', False, 'Offline ERR_INTERNET_DISCONNECTED'),
        ('https://reset.exemple.test/', 'ConnectionReset', False, 'Reset ERR_CONNECTION_RESET'),
        ('https://injoignable.exemple.test/', 'AddressUnreachable', False, 'Unreachable ERR_ADDRESS_UNREACHABLE'),
        ('https://echec.exemple.test/', 'ConnectionFailed', False, 'Refused ERR_CONNECTION_FAILED'),
        ('https://autre.exemple.test/', 'Failed', False, 'Generic ERR_FAILED'),
        ('https://refuse.exemple.test/', 'AccessDenied', False, 'Generic ERR_ACCESS_DENIED'),
        # « HTTPS uniquement »: the https:// attempt of http://bibliotheque-municipale.fr/horaires, refused.
        ('https://bibliotheque-municipale.fr/horaires', 'ConnectionRefused', True, 'Insecure ERR_CONNECTION_REFUSED'),
        (f'https://127.0.0.1:{closed}/', None, True, 'Insecure ERR_CONNECTION_REFUSED'),
    ]


def build() -> tuple[Path, Path]:
    OUT.mkdir(parents=True, exist_ok=True)
    common = [str(TOOLS / 'g++.exe'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-Wno-unknown-pragmas',
              '-Wno-misleading-indentation', '-Wno-cast-function-type', '-static', '-municode', '-Isrc', f'-I{WV2_INCLUDE}']
    host, classify = OUT / 'status_probe_host.exe', OUT / 'status_classify.exe'
    for src, exe, libs in (('tests/error-pages/status_probe_host.cpp', host, ['-lole32', '-luuid', '-luser32']),
                           ('tests/error-pages/status_classify.cpp', classify, ['-lole32'])):
        if subprocess.run([*common, src, '-o', str(exe), *libs], cwd=ROOT).returncode:
            sys.exit(f'FAILED: compile {src}')
    shutil.copy2(WV2_LOADER, OUT / 'WebView2Loader.dll')
    return host, classify


def main() -> int:
    host, classify = build()
    script = subprocess.run([str(classify), '--script'], capture_output=True, text=True, check=True).stdout
    s = socket.socket(); s.bind(('127.0.0.1', 0)); closed = s.getsockname()[1]; s.close()   # nothing listens there
    plan = steps(closed)
    args = ['--inspect', script]
    for url, fail, _, _ in plan:
        args += ['--url', url] + (['--fail', fail] if fail else [])
    profile = Path(tempfile.mkdtemp(prefix='error-status-'))
    try:
        text = run_in_job([str(host), '--udf', str(profile), *args], OUT, 150)
    finally:
        for _ in range(20):
            shutil.rmtree(profile, ignore_errors=True)
            if not profile.exists():
                break
            time.sleep(0.5)
    status, page = {}, {}
    for line in text.splitlines():
        m = re.match(r'STATUS (\S+) fail=\S+ success=\d web=(\d+) http=(\d+)', line)
        if m:
            status[m.group(1)] = (m.group(2), m.group(3))
        m = re.match(r'PAGE (\S+) (.*)$', line)
        if m:
            page[m.group(1)] = m.group(2)
    failures = []
    for url, fail, upgrade, want in plan:
        if url not in status:
            failures.append(f'{url}: no answer'); continue
        web, http = status[url]; doc = page.get(url, '-') or '-'
        got = subprocess.run([str(classify), web, http, doc, 'upgrade' if upgrade else '-'], capture_output=True, text=True).stdout.strip()
        ok = got == want
        print(f'  {"ok  " if ok else "FAIL"} {url:44} {fail or "real request":21} {"(HTTPS uniquement) " if upgrade else ""}engine {web}/{http} {json.loads(doc) if doc != "-" else "-"!s:28} -> {got}')
        if not ok:
            failures.append(f'{url}: {got}, want {want}')
    if not status:
        print(text)
    print('engine failures -> our pages: ' + ('FAILED (' + str(len(failures)) + ')' if failures else f'OK ({len(plan)} failures, each on its page)'))
    for f in failures:
        print('FAILED: ' + f)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
