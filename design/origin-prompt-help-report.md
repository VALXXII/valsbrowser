# Rapport — retrait de « Plus d'infos » des popovers de permission (suivi de PR64)

Branche `VALXXII/permission-help-removal`, basée sur `f97e04a` (fusion PR64 dans Design-missing-elements,
commit local de l'intégrateur, **pas encore poussé ni validé** au moment de ce rapport). Contrat final :
`design/origin-prompt-help.md`.

## 1. Demande et référence

Demande utilisateur : retirer « Plus d'infos » et ce qui l'accompagne. Paper B02 lu en lecture seule après la
modification de l'utilisateur. Aucun popover de permission n'a plus de rangée repliable. La variante
Notifications se compose du titre, du sous-titre et des boutons, sans note ni case. Les autres types gardent
la case « Mémoriser » ; la demande groupée garde ses rangées par type. Capture suivie :
`design/origin-prompt-help-evidence/paper-reference/B02-DQ5-0-user-2026-11-12.png`.

## 2. Modification

* `src/modal.h` : retour exact à `dde6765` (avant PR64), plus l'alias `DialogKit::Glyph::storage`.
  Le mécanisme `Spec::more` n'avait pas d'autre consommateur (vérifié sur `f97e04a` ; le coffre n'en dépend
  pas, confirmé par l'intégrateur ; A10 n'est pas raccordé au code).
* `src/origin-prompt.h` : suppression de `Text::More`, `Spec::requester` et du remplissage `m.more`. Type 13,
  sous-titres et repli 0 / futur conservés ; notifications sans note ni case (mémorisation toujours forcée).
* `src/elements-integration.h` : retour de la ligne `spec.requester` à l'état d'avant PR64.
* Tests : les anciens tests du panneau sont remplacés par des tests de comportement visible (voir §3).

## 3. Preuves (sur l'arbre de cette branche, build 0.3.198)

| Niveau | Fichier | Résultat |
|---|---|---|
| Unitaire, fenêtre réelle | `test-popups.ps1 -SkipPixels` → `modal_test` | 266 vérifications, 0 échec |
| Transport simulé (faux arguments WebView2) | `test-elements-integration.ps1` | PASS |
| Moteur réel isolé (E2E) | `tests/e2e/permission_help_e2e.ps1` | 16/16, runtime 157.0.4341.46, copie jetable, profil neuf |

Détails :
* **modal_test** : nœuds UIA exposés = exactement [Mémoriser, Bloquer, Autoriser, Fermer] pour le stockage
  persistant et la demande groupée, [Bloquer, Autoriser, Fermer] pour les notifications ; aucun nœud Texte ni
  ExpandCollapse. Tab : Bloquer → Autoriser → Fermer → Mémoriser. Molette et ↓ sans effet. Les boutons
  terminent le popover Notifications. Échap ou Abort répondent une seule fois. Notifications Bloquer toujours
  mémorisé. Rendus à 96/144/192 DPI.
* **E2E réel** : le moteur envoie bien `kind=13` et `kind=4`. Titre et sous-titre sont lus par UIA ; aucun
  `PermissionMore`. La case est présente pour le type 13 (témoin positif) et absente pour les notifications.
  La demande reste en attente jusqu'à la décision. Navigation et Fermer donnent `action=0` une seule fois.
  Aucun clic « Autoriser ».
* Le premier passage E2E a échoué sur un contrôle mal écrit : il cherchait la case sur tout le bureau, où
  Paper ouvert l'affiche. Corrigé en limitant la recherche aux processus de la fixture. Le passage suivant a
  vu le popover se fermer quand une autre fenêtre a pris le premier plan (fermeture au clic ailleurs,
  comportement prévu). Après rendu de la recherche UIA tolérante, le passage final est vert.

Sorties suivies : `design/origin-prompt-help-evidence/logs/*.txt` (chemin du runtime ramené à
`%LOCALAPPDATA%`). Rendus : `design/origin-prompt-help-evidence/native/perm-*.png`.

## 4. Limites et points ouverts

* La base `f97e04a` n'est pas encore validée ni poussée par l'intégrateur. Ce correctif est prêt en local et
  ne sera poussé (PR de suivi vers Design-missing-elements) qu'après sa notification.
* Paper : B01 `DEZ-0`, la planche dédiée « B02 · Permissions — Plus d'infos » et sa copie `p-F-0` montrent
  encore la rangée. Elles n'ont pas été touchées (aucune mutation demandée) et ne sont plus le contrat.
* Pas de capture d'écran du popover réel dans l'E2E ; le rendu est prouvé par les PNG hors écran et l'UIA.
