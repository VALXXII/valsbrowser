# Icônes d'application : live et preview

Sources : fichier Paper « Browser Design », page Icon, exportées telles quelles (shaders
HalftoneDots d'origine, coins transparents, rayon 50 px), sans légende ni fond de canvas.

| Fichier | Artboard Paper | Usage |
| --- | --- | --- |
| `live-256.png`, `live-1024.png` | App Icon - Release / Live | version live officielle |
| `preview-256.png`, `preview-1024.png` | App Icon - Beta / Build - Worktrees | builds de branches / worktrees |

`python tools/release/make_app_icons.py` (Pillow requis seulement ici) régénère
`src/icons/browser-live.ico` et `src/icons/browser-preview.ico` : 16, 20, 24, 32, 40, 48,
64, 128 et 256 px, alpha conservé. Le 256 px est l'export 1x tel quel ; les autres tailles
sont une réduction par moyenne de surface de l'export 4x, sans retouche du dessin.

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
