"""Static guard (#127 start-up regression): a `//` comment must never swallow code on the same line.

    python tools/sidebar/check_line_comments.py [repo root]

The browser's sources keep long one-line statement lists (wWinMain's start-up, for example). A `//` comment added in
the middle of one comments out the rest of the line silently: the code still compiles. That is how a trial build lost
`instance=hi; ... CoInitializeEx ...; base=path; ResolveBrowserPaths();` and failed with « Initialisation graphique
native impossible. » (startup_graphics_failed). Two checks, on the real C++ token stream (strings, characters, raw
strings and block comments understood):
  1. no `//` comment that holds a whole block comment followed by code (`// … /* … */ code`), in src/*.cpp and src/*.h;
  2. wWinMain's start-up steps are code (not inside a comment) in src/main.cpp: the instance, DPI awareness, COM, the
     executable's folder (base) and ResolveBrowserPaths, the --tab-layout parse.
Exit code 0 when both hold.
"""
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2])


def scan(text):
    """Returns (code, line_comments): the text with comments and literals blanked, and each `//` comment (line, body)."""
    out, comments = [], []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1; out.append(c); i += 1; continue
        if text.startswith('//', i):
            j = text.find('\n', i); j = n if j < 0 else j
            comments.append((line, text[i + 2:j])); out.append(' ' * (j - i)); i = j; continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2); j = n if j < 0 else j + 2
            chunk = text[i:j]; line += chunk.count('\n'); out.append(re.sub(r'[^\n]', ' ', chunk)); i = j; continue
        m = re.match(r'(?:u8|u|U|L)?R"([^()\\ ]{0,16})\(', text[i:i + 24])
        if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            end = ')' + m.group(1) + '"'; j = text.find(end, i + m.end()); j = n if j < 0 else j + len(end)
            chunk = text[i:j]; line += chunk.count('\n'); out.append(re.sub(r'[^\n]', ' ', chunk)); i = j; continue
        if c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n); out.append(' ' * (j - i)); i = j; continue
        out.append(c); i += 1
    return ''.join(out), comments


failures = []
for path in sorted(list((ROOT / 'src').glob('*.cpp')) + list((ROOT / 'src').glob('*.h'))):
    text = path.read_text(encoding='utf-8', errors='replace')
    _, comments = scan(text)
    for line, body in comments:
        if re.search(r'/\*.*?\*/\s*[A-Za-z_(]', body):   # a block comment opened and closed inside it, then code
            failures.append(f'{path.relative_to(ROOT)}:{line}: a // comment swallows code after « */ »: {body.strip()[:120]}')

main = (ROOT / 'src' / 'main.cpp').read_text(encoding='utf-8', errors='replace')
code, _ = scan(main)
compact = re.sub(r'\s+', '', code)
for step in ('instance=hi;', 'SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);',
             'CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);', 'GetModuleFileNameW(nullptr,path,32768);',
             'base=path;ResolveBrowserPaths();', 'forcedLayout=smoke?-1:TabLayoutArg(args);'):
    if step not in compact:
        failures.append(f'src/main.cpp: wWinMain start-up step missing from the code (commented out?): {step}')

for f in failures:
    print('FAIL', f)
print(f'check_line_comments: {"ok" if not failures else f"{len(failures)} failure(s)"}')
sys.exit(1 if failures else 0)
