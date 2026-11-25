"""« Nouveautés » card: the notes of the release being built (release-notes/README.md).

release-notes/notes.json holds the notes of the NEXT release, French and English, written by whoever
merges the release (the version number does not exist yet: it is the commit count of the merge).
tools/release/build.py calls write_header() on every build: the notes are checked, then compiled into
browser.exe through <out>-obj/gen/release-notes-data.h (src/release-notes.h includes it).

The card's read state keys on notes_id(): a hash of the text. New text = new id = the card opens once
after the update; text left unchanged by a later release = same id = no card (the generic « Mise à jour
installée » notification instead), so old notes are never shown again as new.

  python tools/release/release_notes.py            check release-notes/notes.json, print it (exit 1 on errors)
  python tools/release/release_notes.py --header F write the generated header (what build.py does)
"""
from __future__ import annotations
import argparse, hashlib, json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NOTES = ROOT / 'release-notes' / 'notes.json'
SECTIONS = ('ajouts', 'corrections')   # « Ajouts » / « Corrections » (Paper p-L-0 142E-0 / 144O-0)
LANGS = ('fr', 'en')
MAX_ITEMS = 4    # per section; the Paper card shows 2 and 3, the Full view grows to content (README)
MAX_CHARS = 80   # per line and language: two lines of the 343 DIP card (≈ 70 recommended)


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


def load(path: Path = NOTES) -> dict:
    """The checked notes: {'ajouts': [(fr, en), …], 'corrections': [(fr, en), …]}. SystemExit on any problem."""
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except FileNotFoundError:
        raise SystemExit(f'{path}: introuvable (release-notes/README.md)')
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise SystemExit(f'{path}: JSON illisible : {e}')
    errors = check(data)
    if errors:
        raise SystemExit(f'{path}: notes refusées :\n  - ' + '\n  - '.join(errors))
    return {s: [(i['fr'], i['en']) for i in data[s]] for s in SECTIONS}


def notes_id(notes: dict) -> str:
    """'notes-<16 hex>' of the text (sections, order, both languages); '' when there is nothing to show."""
    if not any(notes[s] for s in SECTIONS):
        return ''
    canonical = json.dumps([[list(p) for p in notes[s]] for s in SECTIONS], ensure_ascii=False, separators=(',', ':'))
    return 'notes-' + hashlib.sha256(canonical.encode('utf-8')).hexdigest()[:16]


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


def header(notes: dict) -> str:
    def lines(section):   # each pair followed by a comma: src/release-notes.h closes the list with a sentinel
        return ' '.join(f'{{{wide_literal(fr)}, {wide_literal(en)}}},' for fr, en in notes[section])
    return ('// Generated by tools/release/release_notes.py from release-notes/notes.json. Do not edit.\n'
            '#pragma once\n'
            f'#define BROWSER_RELEASE_NOTES_ID "{notes_id(notes)}"\n'
            f'#define BROWSER_RELEASE_NOTES_ADDED {lines("ajouts")}\n'
            f'#define BROWSER_RELEASE_NOTES_FIXED {lines("corrections")}\n')


def write_header(target: Path, path: Path = NOTES) -> dict:
    notes = load(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(header(notes), encoding='ascii', newline='\n')
    return notes


def main() -> int:
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--notes', type=Path, default=NOTES)
    p.add_argument('--header', type=Path, help='write the generated header here')
    a = p.parse_args()
    notes = write_header(a.header, a.notes) if a.header else load(a.notes)
    nid = notes_id(notes)
    if not nid:
        print('Aucune note : après la mise à jour, pas de carte « Nouveautés » (notification « Mise à jour installée »).')
        return 0
    for section, title in zip(SECTIONS, ('Ajouts', 'Corrections')):
        if notes[section]:
            print(title)
            for fr, en in notes[section]:
                print(f'  + {fr}  ({len(fr)})\n    {en}  ({len(en)})')
    print(f'OK : {sum(len(notes[s]) for s in SECTIONS)} lignes, identifiant {nid}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
