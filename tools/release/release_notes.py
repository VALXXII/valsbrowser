"""« Nouveautés » card: the notes of every release, per version (release-notes/README.md).

release-notes/notes.json holds the notes of the NEXT release, French and English, written by whoever
merges the release (its version number does not exist yet: it is the commit count of the merge).
The notes of the earlier releases are NOT copied anywhere by hand: they are read back from the history
of that file on main's first-parent chain. Each commit C of that chain that changed the file gives one
entry, keyed by the version built from C (0.3.<git rev-list --count C>): the first build that carried
that text. A release that does not change the file has no entry of its own.

tools/release/build.py calls write_header() on every build: the current file is checked (strictly),
the history is read, and every entry up to this build is compiled into browser.exe through
<out>-obj/gen/release-notes-data.h (src/release-notes.h includes it). After an update the browser shows
the notes of every version newer than the last one whose notes the user saw (cumulative, newest first,
de-duplicated, at most DISPLAY_LINES per section for now): select() below mirrors that choice.

  python tools/release/release_notes.py            check release-notes/notes.json, print it and the history
  python tools/release/release_notes.py --header F --build N   write the generated header (what build.py does)
"""
from __future__ import annotations
import argparse, hashlib, json, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import version as version_mod  # noqa: E402

REL = 'release-notes/notes.json'
NOTES = ROOT / REL
SECTIONS = ('ajouts', 'corrections')   # « Ajouts » / « Corrections » (Paper p-L-0 142E-0 / 144O-0)
LANGS = ('fr', 'en')
MAX_ITEMS = 10      # per section in the file (a catch-up can list more than the card shows)
DISPLAY_LINES = 4   # per section on the card, for now (same constant in src/release-notes.h; design question)
MAX_CHARS = 80      # per line and language: two lines of the 343 DIP card (≈ 70 recommended)


def check(data) -> list[str]:
    """Every problem of a parsed notes.json, in French (the release worker reads them)."""
    errors = []
    if not isinstance(data, dict):
        return ['le fichier doit être un objet JSON { "ajouts": [...], "corrections": [...] }']
    for key in data:
        if key not in SECTIONS:
            errors.append(f'clé inconnue « {key} » (seules « ajouts » et « corrections » existent)')
    for section in SECTIONS:
        items = data.get(section)
        if not isinstance(items, list):
            errors.append(f'« {section} » manque ou n’est pas une liste (mettre [] si rien)')
            continue
        if len(items) > MAX_ITEMS:
            errors.append(f'« {section} » : {len(items)} lignes, {MAX_ITEMS} au maximum (gardez les plus visibles)')
        for n, item in enumerate(items, 1):
            where = f'{section} n° {n}'
            if not isinstance(item, dict) or set(item) != set(LANGS):
                errors.append(f'{where} : il faut exactement {{"fr": "…", "en": "…"}}')
                continue
            for lang in LANGS:
                text = item[lang]
                if not isinstance(text, str) or not text.strip():
                    errors.append(f'{where} ({lang}) : texte vide')
                    continue
                if text != text.strip() or '  ' in text:
                    errors.append(f'{where} ({lang}) : espaces en trop au début, à la fin ou en double')
                if any(ord(c) < 32 or 0x7f <= ord(c) <= 0x9f or c in '  ' for c in text):
                    errors.append(f'{where} ({lang}) : caractère de contrôle ou retour à la ligne interdit')
                if len(text) > MAX_CHARS:
                    errors.append(f'{where} ({lang}) : {len(text)} caractères, {MAX_CHARS} au maximum (deux lignes de la carte)')
                if "'" in text:
                    errors.append(f'{where} ({lang}) : apostrophe droite \' : utilisez l’apostrophe typographique ’')
            if isinstance(item.get('fr'), str) and item.get('fr') == item.get('en'):
                errors.append(f'{where} : l’anglais est identique au français (traduction oubliée ?)')
    return errors


def parse(text: str, where: str) -> dict:
    """{'ajouts': [(fr, en), …], 'corrections': [(fr, en), …]} of a notes.json text. SystemExit on any problem."""
    try:
        data = json.loads(text)
    except json.JSONDecodeError as e:
        raise SystemExit(f'{where}: JSON illisible : {e}')
    errors = check(data)
    if errors:
        raise SystemExit(f'{where}: notes refusées :\n  - ' + '\n  - '.join(errors))
    return {s: [(i['fr'], i['en']) for i in data[s]] for s in SECTIONS}


def load(path: Path = NOTES) -> dict:
    """The checked notes of the next release (the file on disk)."""
    try:
        text = path.read_text(encoding='utf-8')
    except FileNotFoundError:
        raise SystemExit(f'{path}: introuvable (release-notes/README.md)')
    except UnicodeDecodeError as e:
        raise SystemExit(f'{path}: illisible : {e}')
    return parse(text, str(path))


def notes_id(notes: dict) -> str:
    """'notes-<16 hex>' of the text: what builds 0.3.842 to 0.3.898 recorded as « seen » (src/release-notes.h maps
    it back to its version). '' when there is nothing to show."""
    if not any(notes[s] for s in SECTIONS):
        return ''
    canonical = json.dumps([[list(p) for p in notes[s]] for s in SECTIONS], ensure_ascii=False, separators=(',', ':'))
    return 'notes-' + hashlib.sha256(canonical.encode('utf-8')).hexdigest()[:16]


def version_text(build: int) -> str:
    return f'{version_mod.SERIES[0]}.{version_mod.SERIES[1]}.{build}'


def _git(root: Path, *args: str) -> str:
    return subprocess.run(['git', *args], cwd=root, check=True, capture_output=True, text=True,
                          encoding='utf-8').stdout


def history(root: Path = ROOT, head: str = 'HEAD', build: int | None = None, worktree: bool = True,
            warn=lambda m: print(m, file=sys.stderr)) -> list[dict]:
    """Every version that brought notes, newest first: {'build', 'version', 'commit', 'notes', 'id'}.
    From the first-parent history of release-notes/notes.json up to `head`; with `worktree`, an uncommitted change
    of the file counts as the notes of this build (`build`, default: the count of `head`)."""
    entries, seen_builds = [], set()
    try:
        commits = _git(root, 'log', '--first-parent', '--format=%H', head, '--', REL).split()
        head_text = _git(root, 'show', f'{head}:{REL}') if commits else None
        head_build = int(_git(root, 'rev-list', '--count', head).strip())
    except (subprocess.CalledProcessError, FileNotFoundError, ValueError):
        commits, head_text, head_build = [], None, None
    for commit in commits:
        try:
            text = _git(root, 'show', f'{commit}:{REL}')
        except subprocess.CalledProcessError:
            break   # the file was removed there: nothing older is history of this file
        n = int(_git(root, 'rev-list', '--count', commit).strip())
        try:
            notes = parse(text, f'{REL} @ {commit[:10]}')
        except SystemExit as e:
            warn(f'release notes: version {version_text(n)} skipped ({str(e).splitlines()[0]})')
            continue
        if notes_id(notes) and n not in seen_builds:
            entries.append({'build': n, 'version': version_text(n), 'commit': commit, 'notes': notes, 'id': notes_id(notes)})
            seen_builds.add(n)
    if worktree and (root / REL).exists():
        current = parse((root / REL).read_text(encoding='utf-8'), str(root / REL))
        committed = parse(head_text, f'{REL} @ {head}') if head_text is not None else None
        if current != committed and notes_id(current):
            if build is None:   # not committed yet: it ships in a later commit, so after `head`
                n = (head_build or 0) + 1
            else:               # a build of this very tree: the uncommitted text is this build's notes
                n = build
                entries = [e for e in entries if e['build'] != n]
            entries.insert(0, {'build': n, 'version': version_text(n), 'commit': None, 'notes': current, 'id': notes_id(current)})
    entries.sort(key=lambda e: e['build'], reverse=True)
    return entries


def select(entries: list[dict], after: int | None, upto: int, cap: int = DISPLAY_LINES) -> dict:
    """What the card shows (mirror of ReleaseNotes::Unseen): every entry with after < build <= upto (after None:
    all), newest first, lines de-duplicated on their French text, at most `cap` per section (0: no cap)."""
    out = {s: [] for s in SECTIONS}
    for e in sorted(entries, key=lambda e: e['build'], reverse=True):
        if e['build'] > upto or (after is not None and e['build'] <= after):
            continue
        for s in SECTIONS:
            for pair in e['notes'][s]:
                if all(pair[0] != p[0] for s2 in SECTIONS for p in out[s2]) and (not cap or len(out[s]) < cap):
                    out[s].append(pair)
    return out


def wide_literal(text: str) -> str:
    """An ASCII-only C++ wide literal: no source-encoding assumption, no trigraph, no escape left open."""
    out = []
    for c in text:
        o = ord(c)
        if c in '"\\?':
            out.append('\\' + c)
        elif 32 <= o < 127:
            out.append(c)
        elif o <= 0xFFFF:
            out.append(f'\\u{o:04X}')
        else:
            out.append(f'\\U{o:08X}')
    return 'L"' + ''.join(out) + '"'


def header(entries: list[dict]) -> str:
    """The compiled history (src/release-notes.h declares Line and RawEntry before including it)."""
    lines = ['// Generated by tools/release/release_notes.py from the history of release-notes/notes.json. Do not edit.',
             '#pragma once', 'namespace ReleaseNotes::Data {']
    rows = []
    for k, e in enumerate(entries):
        for section, name in (('ajouts', 'Added'), ('corrections', 'Fixed')):
            pairs = ' '.join(f'{{{wide_literal(fr)}, {wide_literal(en)}}},' for fr, en in e['notes'][section])
            lines.append(f'inline const Line k{name}{k}[] = {{{pairs} {{nullptr, nullptr}}}};')
        rows.append(f'    {{"{e["version"]}", "{e["id"]}", kAdded{k}, kFixed{k}}},')
    lines += ['inline const RawEntry kEntries[] = {', *rows, '    {nullptr, nullptr, nullptr, nullptr},', '};',
              '} // namespace ReleaseNotes::Data', '#define BROWSER_RELEASE_NOTES_HISTORY 1', '']
    return '\n'.join(lines)


def write_header(target: Path, build: int | None = None, root: Path = ROOT) -> list[dict]:
    load(root / REL)   # the next release's notes are checked strictly (refused = build refused)
    entries = [e for e in history(root, build=build) if build is None or e['build'] <= build]
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(header(entries), encoding='ascii', newline='\n')
    return entries


def main() -> int:
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--header', type=Path, help='write the generated header here')
    p.add_argument('--build', type=int, help='build number of this build (default: commit count of HEAD)')
    a = p.parse_args()
    if a.header:
        entries = write_header(a.header, a.build)
        print(f'Release notes: {len(entries)} version(s) with notes, newest {entries[0]["version"] if entries else "none"}')
        return 0
    notes = load()
    if not notes_id(notes):
        print('Aucune note pour la prochaine sortie : elle n’ouvrira pas la carte « Nouveautés ».')
    for section, title in zip(SECTIONS, ('Ajouts', 'Corrections')):
        if notes[section]:
            print(title)
            for k, (fr, en) in enumerate(notes[section], 1):
                hidden = '   [pas affichée pour l’instant : 4 par section]' if k > DISPLAY_LINES else ''
                print(f'  + {fr}  ({len(fr)}){hidden}\n    {en}  ({len(en)})')
    entries = history()
    print('\nVersions avec des notes, la plus récente d’abord (ajouts + corrections) :')
    for e in entries:
        counts = ' + '.join(str(len(e['notes'][s])) for s in SECTIONS)
        where = e['commit'][:10] if e['commit'] else 'pas encore commitée : prochaine sortie'
        print(f'  {e["version"]:>10}  {counts:7}  {where}')
    print(f'OK : {sum(len(notes[s]) for s in SECTIONS)} lignes pour la prochaine sortie')
    return 0


if __name__ == '__main__':
    sys.exit(main())
