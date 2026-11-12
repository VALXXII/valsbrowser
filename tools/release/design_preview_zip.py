"""Package the « Éléments & popups » design preview (portable, no profile, no logs).

Usage: python tools/release/design_preview_zip.py [build folder]
Input : build/design-elements-preview (python tools/release/build.py --out build/design-elements-preview --preview),
        or another build folder (when a running preview locks the default one)
Output: build/browser-design-elements-preview.zip (+ .sha256)
"""
from pathlib import Path
import hashlib
import json
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
app = (ROOT / sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/design-elements-preview'
archive = ROOT / 'build/browser-design-elements-preview.zip'
folder = 'design-elements-preview'
version = json.loads((app / 'version.json').read_text(encoding='utf-8'))
names = ['browser.exe', 'update-helper.exe', 'WebView2Loader.dll', 'version.json',
         'WebView2-LICENSE.txt', 'WebView2-NOTICE.txt', 'ThorVG-LICENSE.txt', 'RapidJSON-LICENSE.txt',
         'uBOLite-LICENSE.txt', 'uBOLite-NOTICE.txt']

launcher = '@echo off\r\nrem Ouvre la preview directement sur la sandbox des composants (profil de ce dossier).\r\nstart "" "%~dp0browser.exe" --debug-sandbox\r\n'
instructions = f'''Preview « Éléments & popups » — Windows x64
Build de développement {version["version"]} (commit {version["commit"][:12]}), non publiée, sans mise à jour automatique.

INSTALLATION
1. Extraire TOUT le dossier « {folder} » (garder assets et WebView2Loader.dll à côté du programme).
2. Le runtime Microsoft Edge WebView2 doit être présent (déjà utilisé par le navigateur).
3. Double-cliquer « Ouvrir la sandbox.cmd » : le navigateur s'ouvre directement sur la sandbox.
   Ou lancer browser.exe puis taper browser://debug dans la barre d'adresse, ou Ctrl+Maj+F12.

PROFIL ISOLÉ
Le profil, la session, les réglages et le journal (prototype.log) sont créés DANS ce dossier.
Le navigateur installé, son profil et ses mises à jour ne sont jamais touchés.
Cette preview ne cherche aucune mise à jour et ne redirige jamais vers une copie installée.

SANDBOX DES COMPOSANTS (browser://debug)
Une fiche par artboard Paper (A1–A9, B01–B09, C0–C9), chaque bouton ouvre le VRAI composant natif :
- « Intégré » : composant de production branché sur le vrai événement WebView2
  (alert/confirm/prompt/beforeunload, autorisations, authentification HTTP, application externe,
  Ctrl+F, pages d'erreur réseau, plein écran, effacer les données, fermer la fenêtre, À propos…).
- « Démonstration » : composant de production avec données fictives (mots de passe, certificat
  client, partage d'écran, téléchargement dangereux…). Rien n'est enregistré, effacé ni fermé.
  Les comptes « …demo@exemple.test » sont fictifs et uniquement en mémoire.
- « Natif conservé » : fenêtres système Windows (fichiers, impression, Windows Hello), inchangées.
Le journal à droite affiche le résultat de chaque essai. La page n'accepte de commandes que dans
l'onglet ouvert par le navigateur, avec un jeton propre à ce lancement.

RACCOURCIS UTILES
Ctrl+F rechercher (Entrée / Maj+Entrée, F3), Ctrl+/ raccourcis clavier, F11 plein écran,
Échap quitter le plein écran d'une page, Ctrl+Maj+F12 sandbox.

NOUVEAU DANS CETTE PREVIEW
- Barre d'adresse : puce de demande d'autorisation (caméra, micro, les deux groupés, position,
  notifications, presse-papiers) : en attente pendant le popover, puis « autorisé » (persistant pour la page,
  y compris les choix mémorisés) ou « bloqué » ; clic = état du site avec retrait (recharge la page).
  « En cours » (point bleu) : DÉMONSTRATION seulement (sandbox B02) — WebView2 ne signale pas l'usage caméra/micro.
- Téléchargement dangereux (démo) : « Plus d'options » ouvre le menu, « Conserver quand même… »
  demande confirmation (Annuler par défaut) ; résultat simulé, protections du moteur intactes.
- Partage d'écran (démo) : fenêtre avec catégories et vignettes fictives, « Partager » explicite.
- Certificat client : ligne d'émetteur sans espacement de lettres ; info-bulles et raccourcis sur le fond canvas.
- Pages d'erreur réseau : actions sur une ligne, « Réessayer » à droite ; empilées sous 360 px.

LIMITES CONNUES
Aucune ombre portée sur nos fenêtres : les coins arrondis sont découpés par une région
(bord net, sans anticrénelage DWM) — compromis nécessaire mesuré sous Windows 11.
Gestionnaire de mots de passe : démonstration seulement, aucun identifiant réel n'est lu ni stocké.
'''
(app / 'LIRE-MOI.txt').write_text(instructions, encoding='utf-8-sig')
(app / 'Ouvrir la sandbox.cmd').write_text(launcher, encoding='ascii', newline='')
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for name in names + ['LIRE-MOI.txt', 'Ouvrir la sandbox.cmd']:
        src = app / name
        if not src.is_file():
            raise SystemExit(f'Missing preview payload: {src}')
        z.write(src, f'{folder}/{name}')
    for src in sorted((app / 'assets').rglob('*')):
        if src.is_file():
            z.write(src, f'{folder}/{src.relative_to(app).as_posix()}')
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    bad = [n for n in z.namelist() if '/profile/' in n or n.endswith(('.log', '.tmp')) or n.endswith('/session') or n.endswith('/settings')]
    assert not bad, bad
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(archive.parent / 'browser-design-elements-preview.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
print(f'{archive}\n{archive.stat().st_size} bytes\nSHA256 {digest}')
