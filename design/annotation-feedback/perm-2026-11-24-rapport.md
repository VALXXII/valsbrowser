# Circuit des autorisations du chat — rapport (24 nov. 2026)

Branche `VALXXII/annotation-perm`, partie du bout du lot R (`99c88350`). Faux agent uniquement, rien à l'écran.

## Les causes, une ligne par point

1. **Demandée deux fois.** L'agent et le navigateur n'ont jamais demandé qu'une fois : le journal de l'essai montre une
   demande et une réponse par tour. **Panneau** : le premier clic sur « Autoriser » était perdu, la carte changeait de
   forme entre l'appui et le relâchement (le bouton glissait sous le pointeur). **Bulle** : quand une autre application
   passe devant (capture d'écran, Orca), la bulle est masquée ; le clic qui ramène le navigateur à l'endroit de la bulle
   tombe sur la page, sélectionne un autre élément, la carte disparaît puis revient quand on reclique l'élément.
2. **Carte cassée au clic.** La carte recevait, dès qu'elle prenait le clavier, un nom de style déjà utilisé par la rangée
   de raccourcis de la saisie (`keys`) : titre centré, diff hors de la carte.
3. **« A modifié » avant l'accord.** L'étape en attente d'accord était résumée au passé dans la ligne repliée du fil.
4. **Interrupteur.** Même cause que le point 2 (le clic donne le clavier à la carte).
5. **Le reste du circuit.** La même demande renvoyée par le navigateur pouvait bloquer la réponse ; un refus s'affichait
   « A modifié … (échec) » et restait ainsi en rouvrant la conversation ; le diff ouvert restait affiché après le retrait
   de la demande ; avec peu de place la carte était coupée ; dans la bulle la carte ressemblait à une carte coupée et la
   rangée « mode + Arrêter » disparaissait pendant la demande.

## Ce qui est corrigé

| Commit | Contenu | Sortie |
|---|---|---|
| `9893dcb2` | Points 2, 3, 4 et 1 (panneau) : la carte garde sa forme, un clic répond, « Veut modifier … » jusqu'à la réponse, rien si refusé | intégré à « Ton agent » |
| `24fc4837` | Bulle : la carte est une vraie carte (planche redessinée) | intégré à « Ton agent » |
| `8efb8aa0` | Navigateur : un refus est gardé comme refus (rien dans le fil, même en rouvrant) ; une seule réponse par demande | prochaine sortie |
| `abd4eb8f` | Bulle : le mode et « Arrêter » restent sous la carte ; diff en grand d'une demande = une seule carte ; carte entière même avec peu de place | prochaine sortie |
| `305769ed` | Parcours en vrai navigateur (bureau caché) + lignes proposées pour le test général | prochaine sortie |

Reste à corriger : **point 1 côté bulle** (le clic de retour). C'est dans la fenêtre de la bulle (`src/chat-comment.h`),
pas dans la carte, et cela ne se vérifie qu'en vraie fenêtre. Deux voies : ne plus masquer la bulle quand une autre
application est devant, ou garder le masquage et ne pas prendre ce clic pour une sélection.

## Tests

- Page seule : `python -I tests/chat-panel/run.py` **903 / 903** (bloc « perm », 64 contrôles, seul avec `--only perm` ;
  vrais clics souris ; avant correctif 8 contrôles échouaient). Fidélité aux deux planches redessinées : 0 écart ; rendu à
  100 / 125 / 150 %.
- Navigateur : contrats **147 / 147** (`tests/chat/run_tests.py`), barrière de traduction 0 écart, compilation sans avertissement.
- Vrai navigateur sur bureau caché, faux agent : `tests/chat/permission_circuit.py` **45 / 45** (bulle et panneau) ; le
  parcours clavier du lot 2 pour la bulle **31 / 31**.
- Test général : rien à ajouter au manifeste pour la page et les contrats (suites `chat-panel` et `chat-units`). Les deux
  suites de nuit et les lignes de couverture à ajouter sont dans `design/annotation-feedback/perm-2026-11-24-couverture.json`.

## Fonctions touchées (pour Builder)

`assets/chat-panel/panel.js` : `permCard`, `bPerm` (inchangée, appelée autrement), `permRespond`, `permAnswered` (nouvelle),
`permViewGone` (nouvelle), `onPermission`, `permission.cancel`, `wantOf` / `wantLabel` (nouvelles), `stepTitle`,
`stepsSummary`, `stepLine`, `stepsSeg.render`, `onToolCall`, `onToolUpdate`, `onTurnEnd` (une ligne), `bStatus`,
`renderDockB` (`permFoot`), `publishSize`, `bubbleNatural`. `panel.css` : `.perm:not(.kbd-on)`, bloc `.bperm…`,
`.bub .dock.has-perm`, `.bub #app.view-perm …`, `.bub #app.perm-tight2 / 3`. Navigateur : `chat-host.h` (`Turn`),
`chat-host-turn.h` (`MarkRefused`, `AskedTool`, `RefusingOption`, `OnEvent` : étapes et retrait d'une demande),
`chat-host-ui.h` (`permission.respond`).

## À vérifier en vraie fenêtre

- Le point 1 côté bulle, une fois corrigé.
- La bulle avec une demande est plus haute de 54 px (carte + rangée) : son placement près du bas de la page.
- Un essai réel par agent (Claude Code, Codex) : accepter, refuser, « toujours ».
- Le parcours clavier du lot 2 pour le **panneau** ne tourne pas sur la base du lot R (26 / 33) : le chat est déjà ouvert à
  l'entrée du mode et le clavier n'y est pas donné sur le bureau caché. À reprendre par le lot 2 à sa fusion.

## Pour Designer et le propriétaire

- **Peu de place** : ce qui cède quand la bulle manque de hauteur n'est pas dessiné. Codé : la rangée sous la carte, puis
  le fil, puis le champ « à la place » ; « Refuser » et « Autoriser » restent toujours entiers.
- **Diff en grand** : la poignée de redimensionnement (coin bas droit) chevauche le coin du bouton « Arrêter ».
- **Planche du diff en grand** : valeurs différentes de la carte normale (champ 13 px au lieu de 12, « Refuser » en blanc,
  boutons à 8 px au lieu de 10, 6 px de marge en plus autour de la réponse). Codé tel que dessiné : à confirmer.
- **Mode sous la carte** : la planche écrit « Demander » ; le produit montre l'icône seule (décision R21).
- **Portée de « Toujours autoriser » / « Auto. »** : la carte ne la dit pas. Claude : pour une modification, toutes les
  modifications de la conversation (le mode passe à « Accept edits ») ; pour une commande, une règle durable écrite dans
  les réglages du projet. Codex : la session.
- **Codex, « Additional sandbox permissions »** : le titre arrive en anglais, aucune phrase française validée.
- **Champ « à la place » vide + Entrée = Autoriser** (règle existante du lot 2) : à confirmer, on est dans le champ du refus.
- **Étape en attente** : dans la liste dépliée elle garde l'anneau des étapes en cours (pas d'état dessiné).
