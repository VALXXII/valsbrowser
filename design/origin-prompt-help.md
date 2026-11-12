# Demandes d'autorisation — contrat final (sans aide repliable)

Historique : PR64 (adoptée dans Design-missing-elements en `f97e04a`) avait ajouté une rangée « Plus d'infos »
repliable aux popovers de permission. **Direction utilisateur du 2026-11-12** : « J'ai modifié en enlevant
les modals plus d'infos et tout ça. » La référence est désormais la planche Paper **B02** « Démonstration ·
B02 · Permissions — variantes et états » telle que l'utilisateur l'a modifiée (capture suivie :
`design/origin-prompt-help-evidence/paper-reference/B02-DQ5-0-user-2026-11-12.png`). Ce correctif de suivi
retire la rangée du code. Il garde le reste de PR64 : type 13, sous-titres et repli honnête. Politiques de
mémorisation, deferral, origine et fenêtre privée inchangées.

## 1. Cause vérifiée de la capture x.com (inchangée)

* Journal 0.3.137, lignes `PERMISSION` seulement : `PERMISSION request kind=13 https://x.com`, puis
  `PERMISSION kind=13 action=0`.
* SDK 1.0.4314.35 : `13 = COREWEBVIEW2_PERMISSION_KIND_PERSISTENT_STORAGE` (« permission to persist storage
  created by the Storage APIs, service workers, etc. »). Ce n'était pas une notification.

## 2. Contrat visuel (Paper B02, état utilisateur)

| Popover | Contenu, dans l'ordre |
|---|---|
| Un type avec case (position, presse-papiers, stockage persistant…) | glyphe 16 + titre 16/20 + sous-titre 14/18 ; case « Mémoriser pour ce site » ; Bloquer / Autoriser ; Fermer (×) |
| Notifications | glyphe cloche + titre + sous-titre « Le site pourra afficher des alertes en dehors de la page. » ; Bloquer / Autoriser ; Fermer. **Ni note, ni case** (toujours mémorisé : politique inchangée) |
| Demande groupée | titre « <site> veut utiliser » ; rangées par type ; case ; Bloquer / Autoriser ; Fermer |

Aucune rangée « Plus d'infos », aucun paragraphe repliable, aucun indicateur de défilement.

## 3. Copies conservées (code)

| Type | Titre | Sous-titre |
|---|---|---|
| 13 Stockage persistant | « <o> veut protéger son stockage local » (glyphe base de données) | Protège les données locales du site quand le navigateur libère de l'espace. Les mots de passe sont gérés séparément. |
| 4 Notifications | inchangé | Le site pourra afficher des alertes en dehors de la page. (« même onglet fermé » retiré : non prouvé) |
| Caméra / Micro / Capteurs / Fichiers / Lecture auto / Polices / Fenêtres | inchangés | une phrase courte par type (PR64) |
| 0 inconnu | « <o> demande une autorisation » | Le moteur ne précise pas de quoi il s'agit. |
| > 13 | « <o> demande une autorisation » | Type non reconnu par ce navigateur (code moteur n). |

## 4. Comportement

* Focus initial sur Bloquer ; Tab : [Mémoriser] → Bloquer → Autoriser → Fermer → [Mémoriser].
* UIA : groupe `PermissionPrompt` (nom = titre, aide = sous-titre), case, `PermissionBlock`,
  `PermissionAllow`, `DialogClose` ; pas de nœud ExpandCollapse ni Texte.
* Échap, Fermer, navigation, changement d'onglet = Dismiss une seule fois, rien enregistré. Notifications :
  Bloquer et Autoriser toujours mémorisés.

## 5. Code retiré (consommateurs vérifiés sur `f97e04a`)

`Modal::Spec::more` n'était renseigné que par `OriginPrompt::Build` (permission) ; l'intégrateur confirme que
le coffre n'en dépend pas, et l'aide A10 n'est pas raccordée au code. Retirés : `Disclosure`, `Spec::more`,
`B::More`, `MoreHit`/`MoreTextKey`, mise en page/rendu/clavier/molette/UIA (`WM_APP + 4`), indicateur de
défilement, glyphe `plus`, paramètre `degrees` de `DrawGlyph`, accesseurs `*ForTest` associés,
`OriginPrompt::Text::More`, `Spec::requester` et sa ligne dans `elements-integration.h`. `modal.h` redevient
identique à `dde6765` (avant PR64), plus le seul alias `DialogKit::Glyph::storage`.

## 6. Paper non modifié par ce correctif

L'utilisateur a modifié B02. Ces planches montrent encore l'ancienne rangée : B01 contexte `DEZ-0`, la
planche « B02 · Permissions — Plus d'infos » sur `p-1-0` et sa copie sur `p-F-0`. Elles ne sont plus le
contrat. Je ne les ai ni modifiées ni supprimées (aucune mutation Paper demandée) ; leur nettoyage revient à
l'utilisateur ou au coordinateur.
