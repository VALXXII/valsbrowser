# Carte « Nouveautés » : pourquoi elle ne s'ouvrait plus, et la correction

Branche `VALXXII/fix-release-notes` (depuis main `3fc1cc71`, la sortie 0.3.840). Demande de Val : la carte doit remplacer « Mise à jour installée » après chaque mise à jour et montrer ce que CETTE mise à jour apporte.

## Les deux causes

**1. Le déclencheur.** Après une mise à jour, la carte ne s'ouvre que si le navigateur n'a pas encore vu « ces notes ».
Les notes portaient un identifiant fixe, écrit dans le code : `2026-11-comment-tooltips-download-selectall-typing` (commit `fdd4294e`, 22/11).
Il a été vu une fois, à la mise à jour vers 0.3.646. Il est ensuite resté le même dans toutes les versions. À chaque mise à jour suivante, le navigateur a donc jugé les notes « déjà vues » : pas de carte, et l'ancienne notification « Mise à jour installée » à la place.

Preuves, lues sans rien modifier dans l'installation de Val :
- le fichier `release-notes` des données contient `seen=2026-11-comment-tooltips-download-selectall-typing` ;
- le journal du navigateur : 0.3.646 « RELEASE NOTES startup: show », puis 0.3.690, 0.3.715, 0.3.769 et 0.3.833 « RELEASE NOTES startup: hidden (state ok) ». Le passage à 0.3.840 n'est plus dans le journal (rotation), mais 0.3.840 porte le même identifiant, donc la même décision ;
- `previous` = `installed 0.3.837` et `current` = `0.3.840` : la mise à jour elle-même était bien reconnue (helper, acquittement, version précédente). Seul l'identifiant inchangé bloquait la carte ;
- la carte de la capture a été ouverte à la main, depuis « À propos » (journal : « ABOUT release notes: Nouveautés (full) »).

**2. Le contenu.** Les 5 phrases étaient écrites en dur dans le catalogue de traductions (`release_notes_item_*`), choisies le 22/11 pour la 0.3.641. Aucune étape de sortie ne les touchait : elles sont restées dans chaque version.

Les deux causes ont la même racine : les notes vivaient dans le code et ne faisaient pas partie du processus de sortie.

## La correction

- Les notes vivent maintenant dans **`release-notes/notes.json`** : français et anglais, « ajouts » et « corrections ». Celui qui fusionne une sortie les remplace (procédure : `release-notes/README.md`).
- `tools/release/build.py` vérifie le fichier (nombre de lignes, longueur, traduction, apostrophes) et le compile dans la version (`release-notes-data.h`, généré par `tools/release/release_notes.py`). Un fichier qui ne respecte pas les règles fait échouer le build, donc avant la fusion.
- **L'identifiant est maintenant l'empreinte du texte.** Un nouveau texte ouvre la carte une fois après la mise à jour, à la place de « Mise à jour installée » (ce remplacement existait déjà et ne change pas).
- **Repli sûr, jamais d'anciennes notes.** Si une sortie ne change pas le texte, l'identifiant reste le même : pas de carte, et la notification « Mise à jour installée » s'affiche. Si une version n'a aucune note, elle n'a pas de carte, et « À propos › Notes de version » ouvre la page GitHub de la version.
- Le build de GitHub Actions affiche un avertissement « Release notes » quand une fusion dans main ne change pas les notes.
- Les 5 anciennes phrases sont retirées du catalogue. Elles servent encore d'exemple dans le test natif pour vérifier la géométrie Paper.
- Paper : la mise en page ne change pas (planches `142E-0` et `144O-0`, sections Ajouts puis Corrections). Il n'existe pas de planche qui fixe le nombre de lignes. La règle suit les planches : 4 lignes au maximum par section, en visant 2 ou 3 comme Paper. Chaque ligne fait au maximum deux lignes de la carte.

Pour Val : son fichier contient l'ancien identifiant. À la première mise à jour qui contient cette correction, la carte s'ouvrira donc une fois, avec les notes de cette sortie.

## Les notes de la prochaine sortie (tournée du 25/11)

Écrites dans `release-notes/notes.json` d'après le plan de la tournée (MAJ 24/11 22 h 40). **Une ligne par branche : si une branche ne sort pas, retirez sa ligne.**

| Ligne (FR) | Branche |
| --- | --- |
| Ajouts : Onglets sur le côté : en panneau ou en colonne compacte. | #143 (#127, `VALXXII/127-sidebar`) |
| Ajouts : Nouvelle fenêtre d’impression, avec l’aperçu de vos pages. | `VALXXII/121-print` |
| Ajouts : Si une page ne répond plus, vous pouvez attendre ou la recharger. | `VALXXII/121-crash-screenshare` |
| Corrections : Mode Annotation : la bulle suit l’élément quand la page défile. | lot 1d (ou 1c) |
| Corrections : Le menu des modèles n’est plus coupé par la bulle. | #144 (`VALXXII/128-bubble-menu`) |
| Corrections : Pages d’erreur plus claires : connexion refusée, site non sécurisé. | `VALXXII/121-error-pages-2` |

3 + 3 lignes, comme la maquette Paper (2 et 3) : la carte complète fait 456 DIP.
Pas de ligne pour « Ton agent » et le lot R : leur sortie est conditionnelle. S'ils sortent, ajoutez une ligne dans « ajouts » (4 au maximum).
Pas de ligne non plus pour `fix-resize-border`, qui n'était pas poussée à 22 h 50, ni pour `fix-restore-first-load` (gel du premier lancement, onglets restaurés après une mise à jour qui change le bloqueur de publicités) : moins visible, gardé hors de la carte pour rester à 3 corrections. On peut l'échanger avec une autre ligne : « Vos onglets se rouvrent bien après une mise à jour. » / « Your tabs reopen properly after an update. »
Le Mode Focus, la pastille « Local » et le tutoriel sont déjà sortis en 0.3.840. Ils ne sont donc pas dans ces notes : ils ne sont pas nouveaux pour qui part de 0.3.840.

## Vérifications (sans écran)

- `python -I tests/update-integration/test_release_tools.py` : 65 tests OK, dont les 6 nouveaux (`ReleaseNotesFile` : fichier livré valide, identifiant qui suit le texte, règles, en-tête généré, build, avertissement CI). Ce fichier tourne aussi dans la CI de sortie (`run_all.py`).
- `python -I tools/i18n/check_strings.py` : 0 constat.
- `python tools/release/build.py --out build/fix-release-notes` (dev) puis `--out build/release-notes-e2e --update-repo qa-offline/release-notes-none` (0.3.841) : Build OK, 0 avertissement.
- `test-release-notes.ps1`, lancé sur un bureau Windows caché (rien à l'écran) : 252 vérifications, 3 échecs. Les 3 portent sur « le focus rendu à la fenêtre propriétaire », qui demande le premier plan. Main (`3fc1cc71`) lancé de la même façon donne exactement les 3 mêmes échecs (236 vérifications). Toutes les nouvelles vérifications passent : le cas de Val, les notes inchangées, l'absence de notes, et les notes de ce soir sur deux lignes au plus (FR et EN).
- `tests/e2e/release_notes_e2e.py` (vrai navigateur, racines d'installation fictives, sur un bureau caché) : mis à jour (notes lues dans `notes.json`), avec un nouveau scénario `owner-0.3.840`, le cas de Val (0.3.837 vers la nouvelle version, ancien identifiant déjà vu).
  - Passe complète : 4 échecs. Ils viennent tous de la toute première lecture UIA de la passe (arbre pas encore rempli). Toutes les autres vérifications de ce scénario passent : carte ouverte, notification remplacée, état écrit, Tab, « Voir tout » par UIA, vue complète de 456 DIP, Échap, une seule fois. Tous les autres scénarios passent.
  - Les deux scénarios de mise à jour rejoués (`update-fr` puis `owner-0.3.840`) : 0 échec.
  - Les captures sont indisponibles sur un bureau caché (PrintWindow) : aucune preuve visuelle du vrai navigateur. Les rendus natifs de la carte avec les notes de ce soir sont dans `build/release-notes/release-{full,mini}-{fr,en}-144.png`.
- À refaire pendant la tournée, à l'écran : `python -I tests/e2e/release_notes_e2e.py <build>` (captures et focus réels).
