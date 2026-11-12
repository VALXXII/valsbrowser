# Icônes d'application : live et preview

Sources exportées telles quelles (coins transparents, rayon 50 px), sans légende ni fond de
canvas, jamais retouchées ni régénérées :

| Fichier | Origine | Usage |
| --- | --- | --- |
| `live-1080.png` | export utilisateur « App Icon - Release _ Live@1080p.png » (1080×1080 RGBA, 1 265 443 octets, sha256 `e799d5b82fd2b71f7753af156e07a3c4a6547aa58e338bf8dae48284af92640c`), copié à l'octet près | version live officielle |
| `preview-256.png`, `preview-1024.png` | Paper « Browser Design », page Icon, artboard App Icon - Beta / Build - Worktrees (1x et 4x) | builds de branches / worktrees |

`live-1080.png` remplace les anciens `live-256.png` / `live-1024.png` (Paper, artboard
App Icon - Release / Live) ; c'est l'unique original live, aucune version réduite n'est
versionnée comme source.

`python tools/release/make_app_icons.py` (Pillow requis seulement ici) régénère
`src/icons/browser-live.ico` et `src/icons/browser-preview.ico` : 16, 20, 24, 32, 40, 48,
64, 128 et 256 px, alpha 32 bits conservé, sortie déterministe. Toutes les tailles sont une
réduction par moyenne de surface (box), sans retouche du dessin :

- live : chaque taille, 256 compris, est réduite depuis le master 1080 px ;
- preview : le 256 px est l'export 1x tel quel, les autres tailles viennent de l'export 4x.

## Politique

L'icône fait partie de l'identité de build (`tools/release/version.py`, `APP_ICONS`), jamais
du dossier de lancement :

- `--channel release` → icône live. `build.py` refuse ce channel pour un commit absent de
  `main` (`origin/main` ou `main`). C'est le channel du job CI `update-release` sur main.
- `--channel dev` (défaut de `build.py` et `build.ps1`) → icône preview, pour tout build
  local, de worktree ou de branche, et toute preview portable.

`src/app-icon.rc` choisit l'ICO (`BROWSER_CHANNEL_RELEASE` dans `build-info.h`) pour
`browser.exe` et `update-helper.exe` ; ce dernier est aussi le lanceur stable
`%LOCALAPPDATA%\browser\browser.exe`. Le build copie la même ICO dans `assets/browser.ico` ;
à chaque installation, le helper la recopie en `%LOCALAPPDATA%\browser\browser.ico` (cible
du raccourci du menu Démarrer) et notifie le shell pour ces deux seuls fichiers.

Live et preview ont des exécutables distincts : Windows leur donne des boutons de barre des
tâches séparés, chacun avec son icône, sans AppUserModelID dédié.
