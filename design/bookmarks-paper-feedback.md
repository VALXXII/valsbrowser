# Favoris — retours Paper (6 fils) et décisions de design

Source : Paper « Browser Design », page « Favoris — flow »
<https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-8-0>. Lu le 2026-11-08 : les six fils en entier (dont la réponse de c-mzly9w), JSX et styles calculés.
Exports pour l'intégrateur : `design/bookmarks-paper-exports/` (JSX sans id de nœud, plus `computed-styles.json`).
Ce qui a été modifié par l'utilisateur est **conservé tel quel**. Les corrections sont ciblées, aucun cadre n'a été refait.

## Jetons

| Jeton | Valeur | Rôle |
|---|---|---|
| `--color-browser-accent` (**nouveau**) | `#3E67FF` | Accent personnalisable (futur réglage « couleur d'accentuation »). Utilisé par le curseur texte et le focus. Même valeur aujourd'hui que `--color-browser-split-accent`. |
| `--color-browser-danger` | `#FF7B6E` | Erreur : texte, curseur et contour. |
| `--opacity-browser-focus` | `10%` | Contour du champ focalisé (`#FFFFFF1A`). |

## Fil par fil

### c-qdholr — popover en erreur (artboard 12) : « mettre le lien en ce rouge jusqu'à ce qu'il soit valide »
- **Avant :** URL saisie en `#FFFFFF`, curseur déjà `#FF7B6EE6`, contour inset 1px `#FFFFFF80`.
- **Après (Paper) :** texte URL `var(--color-browser-danger)` = `#FF7B6E`, curseur `#FF7B6EE6` (danger 90 %), contour inset 1px `#FF7B6E80` (danger 50 %). La ligne d'aide reste neutre : glyphe d'avertissement 14px blanc 70 %, texte 14/18 `#FFFFFFB3`. Il n'y a jamais de couleur seule.
- **Comportement à coder :** la validation se fait à l'envoi (« Terminé »). Une fois l'erreur affichée, on revalide **à chaque frappe**. Dès que l'URL est valide, texte, curseur et contour reviennent au blanc, à l'accent et au contour de focus, et le message disparaît. Le champ porte `aria-invalid` et le message sert de description UIA.
- Note de spec de l'artboard 12 mise à jour : l'ancien texte disait « pas de bordure rouge ».
- Même nettoyage d'en-tête que c-ov8ao9, pour que le popover Modifier soit unique : titre retiré, X en absolu 8/8.
- **Statut :** dépend de l'implémentation. **À résoudre après preuve dans le code.**

### c-99tvvr — débordement (artboard 10) : « cet espace vide à gauche… ajouter une icône ici ? »
- **Cause :** la ligne « Gérer les favoris » avait `paddingLeft: 32px` et pas d'icône, donc un trou de 24px dans la colonne des icônes. Le même défaut existait sur « Ouvrir les 6 favoris » (menu de dossier, artboard 05).
- **Décision :** on ajoute une icône qui a du sens plutôt que retirer le dossier au-dessus. Toutes les lignes de menu ont désormais une icône 16px dans la même colonne : `paddingLeft 8`, `gap 8`, icône `16×16` à `flexShrink 0`, blanc 70 %.
  - « Gérer les favoris » : **Phosphor Bold Star (contour)**, la même famille que l'étoile de la barre d'adresse.
  - « Ouvrir les N favoris » : **Phosphor Bold ArrowSquareOut**.
- **Statut :** dépend de l'implémentation, car les menus natifs doivent dessiner ces glyphes. **À résoudre après preuve dans le code.**

### c-ov8ao9 — popover Modifier (artboard 07) : « J'ai modifié un peu le design, pour la croix peux-tu clean ? »
- **Modification de l'utilisateur (conservée) :** titre « Modifier le favori » supprimé. Le popover commence directement par « Nom » au padding 16.
- **Nettoyage de la croix :** l'utilisateur l'avait placée à la main (`left 23.8`, `top 7.8`, conteneur de 286.39px). Elle devient un bouton **24×24 en position absolue, `top: 8px`, `right: 8px`**, glyphe X 14px blanc 50 %, rayon 6. Son glyphe tombe ainsi à 13px du bord, sur la même ligne que le libellé « Nom ».
- Dimensions du popover : 320 de large, padding 16, gap 16, rayon 8, bordure 1px `#FFFFFF0F`, ombre `0 8 24 #00000066`, fond `#171717`. Champs : hauteur 32, padding horizontal 10, rayon 6, fond `#FFFFFF14`. Actions : Supprimer `#FF7B6E` à gauche, Terminé (fond blanc, texte `#171717`, padding horizontal 14) à droite, `paddingTop 4`.
- **Statut :** le design est corrigé. La croix doit aussi être propre dans le code (« clean une fois dans le code »). **À résoudre après preuve dans le code.**

### c-s207a4 — menu clic droit (artboard 06) : « J'ai modifié l'ordre »
L'ordre de l'utilisateur fait foi et n'est pas modifié dans Paper :

1. Ouvrir dans un nouvel onglet
2. — séparateur —
3. Modifier… `F2`
4. Dupliquer
5. Copier le lien
6. — séparateur —
7. Supprimer `Suppr` (texte `#FF7B6E`)
8. — séparateur —
9. Nouveau dossier…
10. Déplacer vers ›
11. Masquer la barre de favoris `Ctrl` `Maj` `B`

Dimensions : 320 de large, padding 4, rayon 10. Lignes de 32 (padding 8/10, rayon 6, texte 14/20 `#FFFFFFE6`, survol `#FFFFFF14` avec texte `#FFFFFF`). Séparateurs de 9 de haut, trait 1px `#FFFFFF0F`, inset 8. Touches : fond `#FFFFFF0F`, rayon 4, padding 2/6, texte 14/16 `#FFFFFF52`, gap 2. Chevron 12px, trait 1.6 `#FFFFFFB3`.
- **Statut :** dépend de l'implémentation. **À résoudre après preuve dans le code.**

### c-mzly9w — curseur texte (artboard 03) + réponse « préparer le terrain pour une couleur d'accentuation »
- **Curseur normal :** 1px de large, entièrement arrondi, couleur `color-mix(var(--color-browser-accent) 90%, transparent)` = `#3E67FFE6`. Hauteur 16 en renommage dans la barre, 18 dans les champs du popover. Appliqué à tous les curseurs de la page (artboards 02, 03 et 07).
- **Curseur invalide :** `#FF7B6EE6` (danger 90 %), comme dans l'écran d'erreur.
- **Préparation dans le code :** une seule source pour l'accent, par exemple `Design::Accent` dans `src/design-system.h`, par défaut `#3E67FF`. Le curseur, l'anneau de focus et les autres usages « accent » la lisent au lieu d'une constante en dur. Le futur réglage n'aura qu'à changer cette valeur.
- **Statut :** dépend de l'implémentation. **À résoudre après preuve dans le code.**

### c-v7q0dg — padding du haut (artboard 01) : « pas censé avoir 4px de padding vers le haut »
- **Zone exacte :** l'espace entre la bordure basse du chrome et le haut de la page web.
- **Paper :** les cadres « Contenu · … » des artboards 01 à 03 avaient déjà `paddingTop 0`. Ceux des artboards avec barre de favoris (04 à 12, 9 cadres) avaient `padding: 4px` sur les quatre côtés. Ils sont passés à **`paddingTop 0`, `paddingInline 4`, `paddingBottom 4`**.
- **Géométrie :** chrome sans barre = 2 + 36 + 6 + 32 + 4 + 1 de bordure = **81**. Avec barre = 81 + 6 + 34 = **121**. Le haut de la page web doit être **exactement** le bas du chrome.
- **Code actuel, source du bug :** `src/main.cpp:207` contient `RECT bounds{S(4),S(84),S(chrome.width-4),…}`. Le 84 vaut 80 + 4 de padding. Il faut utiliser le bas du chrome : 81 sans barre, 121 avec la barre de favoris. Gauche, droite et bas restent à 4.
- **Statut :** dépend de l'implémentation. **À résoudre après preuve dans le code.**

## Comptes rendus de revue (captures prises après chaque changement)
- **Popover en erreur :** le texte, le curseur et le contour rouges se lisent clairement, et l'aide neutre garde la hiérarchie. Le popover est resté ancré sous la puce ; un décrochage passager a été corrigé en remettant `position: absolute`.
- **Popover Modifier :** la croix est alignée à 8/8 et sur la ligne de « Nom ». Rien d'autre n'a bougé.
- **Menus débordement et dossier :** les icônes s'alignent sur la colonne des favicons et des dossiers. Le contraste à 70 % est identique à celui des dossiers.
- **Contenu :** la page web touche le chrome sur les 12 artboards. Les marges de 4 à gauche, à droite et en bas sont conservées.
- **Menu réordonné :** aucune modification.

## Résolution des fils
Mise à jour après intégration : les six fils (`c-qdholr`, `c-99tvvr`, `c-ov8ao9`, `c-s207a4`, `c-mzly9w`, `c-v7q0dg`) sont désormais résolus dans Paper. Les statuts intermédiaires ci-dessus décrivent la livraison design initiale ; les preuves de code, captures natives et tests sont consignées dans `design/bookmarks-preview-validation.md`.
