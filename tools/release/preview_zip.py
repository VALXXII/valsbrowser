"""Package the local bookmarks preview without profiles, logs or personal data."""
from pathlib import Path
import hashlib
import zipfile

ROOT = Path(__file__).resolve().parents[2]
app = ROOT / 'build/bookmarks-preview'
archive = ROOT / 'build/browser-bookmarks-preview.zip'
names = ['browser.exe', 'update-helper.exe', 'WebView2Loader.dll', 'version.json',
         'WebView2-LICENSE.txt', 'WebView2-NOTICE.txt', 'ThorVG-LICENSE.txt', 'RapidJSON-LICENSE.txt',
         'Gui-LICENSE.txt', 'Gui-NOTICE.txt']   # chat: logic adapted from Gui (Apache-2.0), tools/release/build.py LICENSES
instructions = '''Preview Favoris - Windows x64

Extraire tout ce dossier, puis lancer browser.exe.
Garder assets et WebView2Loader.dll a cote du programme.
Le runtime Microsoft Edge WebView2 doit etre present (deja utilise par le navigateur).

Profil de test separe, cree a cote de browser.exe. Aucun favori personnel inclus.
Ctrl+D : enregistrer la page / modifier son favori.
Ctrl+Maj+B : afficher ou masquer la barre.
Ctrl+Maj+O : ouvrir le tiroir Favoris.
Clic droit : dossier, deplacement, duplication, suppression.
Ctrl+Z et Annuler : annuler la derniere operation.

Build dev local 0.3.71, changements de la branche Bookmarks non fusionnes.
Le selecteur de dossiers du popover est un menu arborescent natif.
La couleur d'accent est centralisee et sauvegardee ; le futur selecteur de couleur
dans les reglages ne fait pas partie de cette preview.
'''
(app / 'LIRE-MOI.txt').write_text(instructions, encoding='utf-8')
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for name in names + ['LIRE-MOI.txt']:
        src = app / name
        if not src.is_file():
            raise SystemExit(f'Missing preview payload: {src}')
        z.write(src, f'bookmarks-preview/{name}')
    for src in sorted((app / 'assets').rglob('*')):
        if src.is_file():
            z.write(src, f'bookmarks-preview/{src.relative_to(app).as_posix()}')
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert not any('/profile/' in n or n.endswith(('.log', '.tmp')) for n in z.namelist())
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(archive.parent / 'browser-bookmarks-preview.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
print(f'{archive}\n{archive.stat().st_size} bytes\nSHA256 {digest}')
