# Favoris — preview native

Correctif du 9 novembre : le toast d'annulation désactive explicitement le backdrop système (DWMSBT_NONE) et le cadre non-client DWM. Sa fenêtre est découpée au rayon natif de la carte (10 DIP), recalculé lors des changements de taille/DPI ; les frames d'animation réutilisent ce masque. Le fond rectangulaire ne peut plus apparaître dans les coins. Régression vérifiée via GetWindowRgn/PtInRegion : les quatre coins sont exclus et le centre reste dans la fenêtre (`test-bookmarks-ui.ps1`, log `build/toast-radius-qa.log`).

Build Windows x64 dev, version 0.3.71, branche Bookmarks. Changements locaux non fusionnés. La version est calculée sur HEAD ; le binaire inclut les modifications non commitées de cette branche.

## Livrables

- Exécutable portable : `build/bookmarks-preview/browser.exe`.
- Archive : `build/browser-bookmarks-preview.zip`, avec DLL WebView2, ressources, fontes et licences. Extraire le dossier entier avant lancement.
- Profil séparé dans `build/bookmarks-preview/profile` ; favoris dans `build/bookmarks-preview/bookmarks`. Aucun accès au profil installé lors des tests de la preview. L'archive exclut profil, sessions, favoris, logs et données de tests.
- Reproduction : `python tools/release/build.py --out build/bookmarks-preview --channel dev`, puis `python tools/release/preview_zip.py`.

## Implémentation

Store pur C++ versionné, deux racines, dossiers ordonnés, protection contre cycles/profondeur excessive, sauvegarde atomique et récupération des fichiers endommagés. Barre avec débordement et réordonnancement, étoile/Ctrl+D, popover natif, menus en cascade, recherche dans le tiroir, duplication, déplacement, suppression et pile d'annulation. Menus exposés à UI Automation, dossiers ExpandCollapse et étoile Toggle.

Les six retours Paper sont appliqués :

| Retour | Code et vérification |
| --- | --- |
| URL rouge jusqu'à correction | `src/bookmark-popover.h`, validation à l'envoi puis à chaque frappe, texte/contour/curseur danger et aide UIA. Capture `popover-invalid.png`, test de retour au blanc quand valide. Aide répartie sur deux lignes pour éviter sa troncature. |
| Gouttière vide du débordement | `src/main.cpp` : icônes Star et ArrowSquareOut ; `src/context-menu.h` : colonne d'icônes. Capture `menu-overflow-560.png`. |
| Croix du popover | `src/bookmark-popover.h`, cible 24 DIP ancrée à 8/8, titre retiré conformément au tweak utilisateur. Capture `popover-valid.png`. |
| Ordre du menu | `BookmarkContextMenu` dans `src/main.cpp`, ordre comparé à l'export Paper par un client UIA réel. Capture `menu-context.png`. Le parseur des raccourcis a aussi été corrigé : « Suppr » ne devient plus une flèche Haut parce qu'il contient « up ». |
| Curseur et futur accent | `Design::Accent` dans `src/design-system.h`, `NativeUI::AccentCaret`, réglage `accent=#RRGGBB` dans `src/settings.h`, lecture et écriture vérifiées par aller-retour. Aucun nouveau sélecteur de couleur dans cette preview. |
| Padding supérieur | `chrome.ContentTop()` dans le layout WebView2, 81 DIP sans barre, 121 avec barre, sans ajout de 4 DIP en haut. Vérifié par géométrie native et smoke WebView2. |

Les six fils Paper ont été marqués résolus après les preuves de code et de tests. Les modifications de design de l'utilisateur sont préservées ; exports précis dans `design/bookmarks-paper-exports`.

## Vérification

| Commande | Résultat |
| --- | --- |
| `test-bookmarks.ps1` (aussi intégré dans `test.ps1`) | PASS : Unicode, échappement, 10 000 nœuds, cycles, profondeur, déplacements, annulation, fichiers corrompus/tronqués. |
| `test-bookmarks-ui.ps1` | PASS : client UIA réel, barre/étoile/menus/sous-menus/popover/tiroir, notifications, clavier, drag, undo, restauration et persistance. Rendus 96/144/192 DPI, largeurs 560/1180 DIP. |
| `test.ps1` | PASS : modèle, session, réglages, adresses et smoke du vrai navigateur WebView2, dont parcours favoris. Log `build/smoke-resume.log`. |
| `test-ui.ps1` | PASS : 712 114 contrôles de rendu natif, 54 contrôles de motion, fond EDIT et 88 contrôles du champ d'adresse. Log `build/ui-resume.log`. |
| `test-design-system.ps1` | PASS : design system, états de survol et de focus. Log `build/design-resume.log`. |
| `test-accessibility.ps1` | PASS : lecteur UIA du chrome et champ natif nommé. Log `build/accessibility-resume.log`. |
| Build preview puis `browser.exe --smoke-test` | PASS : démarrage du binaire produit, runtime WebView2 chargé et `SMOKE PASS` dans son log. |

Captures inspectées : popover valide/invalide, menu contextuel, débordement à 560 DIP et tiroir développé (`build/bookmarks-ui-qa`). Les captures attendent la fin de l'entrée animée pour ne pas juger un état flouté intermédiaire.

Corrections du harness pendant reprise : le parcours récursif UIA copiait désormais le nom du parent au lieu de garder une référence invalidée par la réallocation du vecteur ; le sous-menu contient bien trois lignes (ouvrir les deux favoris, puis les deux liens) ; l'écoute des notifications est installée avant les interactions pour éviter la course d'enregistrement asynchrone.

## Limites de la preview

Le choix de destination du popover utilise un menu natif arborescent accessible plutôt qu'une expansion de l'arbre dans le popover du storyboard. Le renommage des dossiers passe par le popover ou F2. Les suggestions d'adresse ne classent pas encore les favoris dans une section dédiée ; la recherche dédiée est dans le tiroir. Aucun test utilisateur avec Narrateur/NVDA, contraste exhaustif, RTL ou zoom texte 200 % n'est revendiqué ; UIA et DPI ont été testés automatiquement.

Le worker Claude a atteint sa limite de session pendant l'intégration ; la reprise, les corrections finales, la compilation et l'emballage ont été terminés par le coordinateur. Aucun merge, déploiement ni remplacement du navigateur installé.
