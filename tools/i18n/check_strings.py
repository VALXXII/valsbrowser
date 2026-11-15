"""Interface language coverage gate (design/browser-languages-plan.md).

1. Catalog (src/i18n-catalog.inc): unique ids, French and English present and non-empty, the same
   {n} placeholders, plural pairs id_one / id_other complete, English differs from French unless
   the id is a known brand/autonym (SAME_OK).
2. Sources (src/*.h, src/*.cpp): every string literal that looks like interface text must come
   from the catalog (TR / TRF / TRP). A literal is accepted outside the catalog only when it is
   provably not interface text: a log line, an identifier/path/URL/markup token, a known brand or
   technical name (TOKENS), a whole file classified in FILES (with its reason), or a line marked
   « i18n-exempt: <reason> » (the reason is mandatory).

  python tools/i18n/check_strings.py            gate (exit 1 on any finding)
  python tools/i18n/check_strings.py --report   per-file counts, never fails
"""
from __future__ import annotations
import argparse, re, sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'src'
CATALOG = SRC / 'i18n-catalog.inc'

# Whole files that hold no interface text of the browser, with the reason (audited).
FILES = {
    'i18n-catalog.inc': 'the catalog itself',
    'debug-sandbox.h': 'internal QA page browser://debug (French review tool; the production components it opens are translated)',
    'motion-lab.h': 'review-only motion lab (--motion-lab), never in a user flow',
    'motion-tuning.h': 'motion lab persistence (keys, review tool)',
    'password-demo.h': 'B05 demonstration surface with fictitious accounts, opened only from the QA sandbox',
    'e2e-fixture-pin.h': 'end-to-end test hook compiled only into test hosts',
    'mcp-bridge.cpp': 'MCP stdio bridge: protocol schema, tool descriptions and errors for agents (developer text)',
    'strict-json.h': 'JSON parser diagnostics (protocol)',
    'annotation-script.h': 'script injected in pages: protocol messages and CSS; visible strings come from the host',
    'audio-script.h': 'script injected in pages (protocol, no visible text)',
    'svg-path.h': 'SVG path data',
    'update-package.cpp': 'package format and helper protocol (errors are logged, not shown)',
    'annotations-migration.h': 'migration record format',
    'trace-erase.h': 'trace/log helpers',
    'accessibility.h': 'UIA provider plumbing (names come from callers)',
    'annotations.cpp': 'annotation store and its Markdown/MCP documents: data written for agents (stable French schema), '
                       'event summaries stored as data, store error messages for agents (the browser UI never shows them: '
                       'it maps Outcome::reason / code to catalog text, src/annotation-problem.h)',
}

# Known non-interface literals: brands, product/engine names, fonts, technical tokens.
TOKENS = {
    'browser', 'WebView2', 'Microsoft Edge WebView2', 'Inter Display', 'Segoe UI', 'Segoe UI Variable', 'Segoe Fluent Icons',
    'Segoe MDL2 Assets', 'JetBrains Mono', 'DuckDuckGo', 'Google', 'Brave Search', 'Bing', 'Ecosia', 'Qwant', 'Claude',
    'Claude Desktop', 'Codex', 'OpenAI', 'Anthropic', 'GitHub', 'Windows', 'English', 'Français', 'uBlock Origin Lite',
    'ThorVG', 'Lottie', 'MCP', 'HTTPS', 'HTTP', 'JSON', 'TOML', 'PDF', 'URL', 'Ctrl', 'Alt', 'Tab',   # language-neutral keys only: no Maj/Shift/Entrée/Échap/Espace
    'F11', 'F5', 'F12', 'Microsoft Corporation', 'Consolas', 'Cascadia Mono',
}

LITERAL = re.compile(r'(?:\b(?:L|u8|u|U))?R"([^(\s]{0,16})\((.*?)\)\1"|(?:\b(?:L|u8|u|U))?"((?:[^"\\\n]|\\.)*)"|\'(?:[^\'\\\n]|\\.)*\'|//[^\n]*|/\*.*?\*/', re.S)
WORD = re.compile(r"[A-Za-zÀ-ÿ][A-Za-zà-ÿ'’]+")
CAPWORD = r'[A-ZÀ-Ý][a-zà-ÿ]{2,}(?:-[a-zà-ÿ]{2,})*[.!?…]?' 
ACCENT = re.compile(r'[À-ÿ«»…’—›]')
# Diagnostics: logs, traces, and main.cpp's Fail/Check(hr, where) whose « where » is the failing API name
# (the dialog's own wording around it is catalog text). Free functions only: a member call such as
# profileDeleteDialog.Fail(...) or p->Check(...) is UI, never a diagnostic.
LOGISH = re.compile(r'(?<![.>])\b(Log|LogLine|LogW|Trace|OutputDebugString[AW]?|Fault|MigrationStepFailed|Diag|Debug|Fail|Check|EngineDropped|WorkspaceEngineFailed|static_assert|SplitFail|SplitCheck|E2EHostAbort|ProfilesQaFail|WsqaFail|LogPermissionHr|MenuFail|unavailable|SandboxLog)\s*\(|\bstd::(cerr|clog)\b|\bthrow\b|\bSmoke\b')
# A narrow (char) literal that is a French sentence: two or more common French words.
FRENCH_NARROW = re.compile(r'(?i)(?:\b(?:de|du|des|le|la|les|est|pas|sur|dans|une?|illisible|impossible|cliquez|configuration de)\b.*){2,}')
TR_CALL = re.compile(r'\bTR[FP]?\(')


def unescape(s: str) -> str:
    try:
        return bytes(s, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')
    except Exception:
        return s


def looks_textual(text: str) -> bool:
    t = text.strip()
    if re.search(r'(^|\+)(Maj|Shift|Entrée|Enter|Échap|Espace|Space|Suppr|Retour arrière)(\+|$)', t):
        return True                       # a key name that depends on the language (« Ctrl+Maj+A » / « Ctrl+Shift+A »): use the key_* entries
    if text != t and re.fullmatch(r"[a-zà-ÿ]+(?: [a-zà-ÿ]+)*", t):
        return True                       # a lowercase word glued into a sentence (L" sur ", L" of "): use a whole template
    if not t or t in TOKENS or not re.search(r'[A-Za-zÀ-ÿ]', t):
        return False                      # separators and punctuation only (« › », « · »)
    if re.search(r'://|\\\\|^[\w.\-]+\.(png|svg|ico|json|txt|exe|dll|ttf|html?|js|css|ini|lsrupd|mcpb|toml)$', t):
        return False
    if re.fullmatch(r'[#%]?[\w.\-:/+*=<>()\[\]{}|,;@$^&!?~]*', t) and not re.fullmatch(CAPWORD, t) and not ACCENT.search(t):
        return False                      # identifiers, keys, formats, single lowercase ASCII tokens (« déconnecté » is French text)
    if re.fullmatch(r'[a-z0-9]+(-[a-z0-9]+)+ \d+', t):
        return False                      # file format headers (« browser-settings 1 »)
    if not ACCENT.search(t) and (re.search(r'[{};=<>|\\]|=>|%[-+ #0-9.]*[a-z]|\bfunction\b|^\s*[MmLlHhVvCcZz][\d\s.,-]', t) or re.fullmatch(r'[\d\s.,MmLlHhVvCcSsQqTtAaZz-]+', t)):
        return False                      # code, markup, format strings, path data (no French letters)
    if ACCENT.search(t):
        return True
    words = WORD.findall(t)
    if len(words) >= 2 and ' ' in t:
        return True
    return bool(re.fullmatch(CAPWORD, t))


def raw_textual(body: str) -> list[str]:
    """Visible text inside markup/script raw strings: text between tags and quoted JS/attribute strings."""
    found = []
    for m in re.finditer(r'>([^<>{}]+)<', body):
        if looks_textual(m.group(1)) and (ACCENT.search(m.group(1)) or len(WORD.findall(m.group(1))) >= 2):
            found.append(m.group(1).strip())
    for m in re.finditer(r'(?:aria-label|title|placeholder|alt)="([^"]+)"', body):
        if looks_textual(m.group(1)):
            found.append(m.group(1))
    for m in re.finditer(r"'([^'\n]{3,})'", body):
        if ACCENT.search(m.group(1)):
            found.append(m.group(1))
    return found


def diagnostic_spans(text: str) -> list[tuple[int, int]]:
    """Character spans that are diagnostics: the ARGUMENTS of Log(...)/Fail(...)/… calls (to the matching
    parenthesis, across lines), and « std::cerr … ; » / « throw … ; » statements. Only literals inside a span
    are skipped: a Log(...) on a line never hides an Announce(...) or any other visible text beside it."""
    spans = []
    for m in LOGISH.finditer(text):
        if m.group(0).endswith('('):
            depth, i, n = 1, m.end(), len(text)
            while i < n and depth:
                ch = text[i]
                if ch == '"' or (ch == "'" and not text[i - 1].isalnum()):
                    if ch == '"' and text[i - 1] == 'R' and (i < 2 or not text[i - 2].isalnum() or text[i - 2] == 'L'):
                        d = text.find('(', i)
                        end = text.find(')' + text[i + 1:d] + '"', d)
                        i = (end + len(text[i + 1:d]) + 2) if end >= 0 else n
                        continue
                    j = i + 1
                    while j < n and text[j] != ch and text[j] != '\n':
                        j += 2 if text[j] == '\\' else 1
                    i = j + 1
                    continue
                if ch == '(':
                    depth += 1
                elif ch == ')':
                    depth -= 1
                i += 1
            spans.append((m.start(), i))
        else:                               # std::cerr / throw / Smoke: up to the end of the statement
            end = text.find(';', m.end())
            spans.append((m.start(), end if end >= 0 else len(text)))
    return spans


def scan_file(path: Path) -> list[tuple[int, str]]:
    return scan_text(path.read_text(encoding='utf-8', errors='replace'))


def self_test() -> list[str]:
    """The gate's own regressions: a diagnostic call must never hide visible text beside it."""
    cases = [
        ('Log(L"ANNOTATE reopen"); Announce(r ? L"Annotation rouverte" : L"Impossible de rouvrir : ");', 2),
        ('Announce(ok ? AppLong(a) + L" déconnecté" : msg);', 1),
        ('Log(L"PROFILES refused " + id + L": pending"); VaultNotice(L"Une suppression est en cours", L"Attendez.");', 2),
        ('Log(L"Opération réussie " +' + chr(10) + '    L"détails");', 0),
        ('Log(L"x"); // a comment with Accentué text', 0),
        ('n.accelerator = L"Ctrl+Maj+A"; k = L"Shift+F10"; e = L"Échap";', 3),
        ('profileDeleteDialog.Fail(L"Rien n’a été effacé.");', 1),
        ('Fail(L"CreateCoreWebView2Environment", hr);', 0),
        ('return Make(Result::Failed, "Configuration de Claude illisible.", configPath);', 1),
        ('return Error("invalid header"); Log("ANNOTATE store error"); w.Key("message");', 0),
        ('auto t = name + L", " + std::to_wstring(i) + L" sur " + std::to_wstring(n);', 1),
    ]
    errors = []
    for src, want in cases:
        got = len(scan_text(src))
        if got != want:
            errors.append(f'self-test: {want} finding(s) expected, {got} found in: {src!r}')
    return errors


def scan_text(text: str) -> list[tuple[int, str]]:
    lines = text.split('\n')
    findings = []
    # Whole regions classified non-UI: « // i18n-exempt-begin: <reason> » … « // i18n-exempt-end ».
    exempt = []
    for b in re.finditer(r'//\s*i18n-exempt-begin:\s*\S[^\n]*', text):
        e = text.find('i18n-exempt-end', b.end())
        exempt.append((b.start(), e if e >= 0 else len(text)))
    diagnostics = diagnostic_spans(text)
    for m in LITERAL.finditer(text):
        if any(a <= m.start() < z for a, z in exempt):
            continue
        tok = m.group(0)
        if tok.startswith('//') or tok.startswith('/*') or tok.startswith("'"):
            continue
        line_no = text.count('\n', 0, m.start()) + 1
        line = lines[line_no - 1]
        prev = lines[line_no - 2] if line_no >= 2 else ''
        if 'i18n-exempt:' in line or 'i18n-exempt-next:' in prev:
            continue
        if any(a <= m.start() < z for a, z in diagnostics) or line.lstrip().startswith('#include') or line.lstrip().startswith('#define BROWSER'):
            continue
        if m.group(2) is not None:      # raw string
            for t in raw_textual(m.group(2)):
                findings.append((line_no, t))
            continue
        value = unescape(m.group(3) or '')
        wide = tok.startswith('L"') or tok.startswith('u8"') or tok.startswith('u"') or tok.startswith('U"')
        before = text[max(0, m.start() - 40):m.start()]
        if not wide and not ACCENT.search(value):
            # Narrow ASCII strings are protocol, file formats and internal errors (the UI is wide text), EXCEPT
            # messages that flow to the UI (agent Report.message through Make/Refused, « .message = ») and
            # narrow sentences that read as French.
            to_ui = re.search(r'(\bMake\(\s*[\w:]+\s*,\s*|\bRefused\(\s*|\bmessage\s*=\s*)$', before)
            if not ((to_ui and looks_textual(value)) or FRENCH_NARROW.search(value)):
                continue
        if re.search(r'(automationId|lpszClassName|ClassName|className|IID_\w+|activityId)\s*(=|\+)?\s*\(?$|automationId\s*\+\s*$', before.rstrip()):
            continue                    # UIA automation ids and window class names are identifiers
        if looks_textual(value):
            findings.append((line_no, value))
    return findings


def check_catalog() -> list[str]:
    errors, seen = [], set()
    entry = re.compile(r'^I18N\((\w+),\s*L"((?:[^"\\]|\\.)*)",\s*L"((?:[^"\\]|\\.)*)"\)\s*(//\s*same:\s*\S.*)?$')
    ids = []
    for n, line in enumerate(CATALOG.read_text(encoding='utf-8').splitlines(), 1):
        s = line.strip()
        if not s or s.startswith('//'):
            continue
        m = entry.match(s)
        if not m:
            errors.append(f'{CATALOG.name}:{n}: not a one-line I18N(id, L"fr", L"en") entry')
            continue
        ident, fr, en, same = m.groups()
        ids.append(ident)
        if ident in seen:
            errors.append(f'{CATALOG.name}:{n}: duplicate id {ident}')
        seen.add(ident)
        if not fr.strip() or not en.strip():
            errors.append(f'{CATALOG.name}:{n}: {ident}: empty message')
        ph = lambda s: sorted(set(re.findall(r'\{\d\}', s)))
        if ph(fr) != ph(en):
            errors.append(f'{CATALOG.name}:{n}: {ident}: placeholders differ {ph(fr)} vs {ph(en)}')
        if fr == en and not same and not re.fullmatch(r'[\W\d{}]*', fr) and fr not in TOKENS:
            errors.append(f'{CATALOG.name}:{n}: {ident}: English identical to French (mark the line « // same: <reason> » if intended)')
        if re.search(r'[à-ÿ]', en) and ident not in ('language_keywords',) and 'Français' not in en and not same:
            errors.append(f'{CATALOG.name}:{n}: {ident}: accented letter in the English text (untranslated?)')
    for ident in ids:
        if ident.endswith('_one') and ident[:-4] + '_other' not in seen:
            errors.append(f'{ident}: plural without _other')
        if ident.endswith('_other') and ident[:-6] + '_one' not in seen:
            errors.append(f'{ident}: plural without _one')
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--report', action='store_true')
    ap.add_argument('--file', action='append')
    a = ap.parse_args()
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')   # findings carry French text (cp1252 consoles)
    errors = self_test() + check_catalog()
    counts = Counter()
    files = sorted(p for p in SRC.iterdir() if p.suffix in ('.h', '.cpp', '.inc'))
    for path in files:
        if path.name in FILES or (a.file and path.name not in a.file):
            continue
        for line_no, value in scan_file(path):
            counts[path.name] += 1
            if not a.report:
                errors.append(f'src/{path.name}:{line_no}: untranslated interface text: {value!r}')
    if a.report:
        for name, n in counts.most_common():
            print(f'{n:5} {name}')
        print(f'{sum(counts.values())} candidate literals; catalog errors: {len(errors)}')
        for e in errors:
            print(e)
        return 0
    for e in errors:
        print(e)
    print(f'i18n coverage: {len(errors)} finding(s); {len(FILES)} files classified non-UI')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
