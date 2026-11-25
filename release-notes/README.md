# Notes de version : la carte « Nouveautés »

Après une mise à jour, le navigateur ouvre une fois la carte « Nouveautés » en bas à droite, à la place de la notification « Mise à jour installée ».
La carte montre les notes de **toutes les versions que la personne n'a pas encore vues**, la plus récente d'abord. Une même ligne n'apparaît qu'une fois.
Elle garde **10 lignes par section au plus** : d'abord celles de la version la plus récente, puis celles des versions plus anciennes.

Ce que montre la carte (maquettes Paper « Notes de version — modale », validées le 25/11) :
- la petite carte (343 × 218) se termine par le bouton « Voir les 20 nouveautés » (le nombre de lignes de la carte ; une seule ligne : « Voir la nouveauté ») ;
- la carte complète fait au plus 480 de haut. Au-delà, le texte défile sous l'en-tête, avec un fondu en bas et une barre fine qui n'apparaît que pendant le défilement ;
- quand la carte réunit plusieurs versions, « Depuis la version 0.3.646 » s'affiche sous le titre : la dernière version dont la personne a vu les notes. Une seule version : pas de sous-titre.

## À chaque sortie (à faire par celui qui fusionne dans main)

1. Avant la fusion, **remplacez** le contenu de `release-notes/notes.json` par les nouveautés de cette sortie, et seulement elles.
   Ne gardez jamais les lignes de la sortie précédente : elles sont déjà dans l'historique (voir plus bas), et les garder les remontrerait comme nouvelles.
   Ne mettez que ce qui part vraiment. Retirez les lignes d'une branche qui ne sort pas.
2. Vérifiez avec `python tools/release/release_notes.py`. La commande affiche chaque ligne avec sa longueur, liste les versions qui ont des notes, puis dit « OK ».
   Le build (`tools/release/build.py`) refuse lui aussi un fichier qui ne respecte pas les règles ci-dessous.
3. Commitez le fichier dans la branche qui est fusionnée. Les notes d'une sortie s'écrivent en une fois, dans la fusion de cette sortie.

Si une sortie ne modifie pas ce fichier, elle n'a pas de notes à elle. Ceux qui avaient déjà tout vu voient la notification « Mise à jour installée ».
Le build de GitHub Actions le signale par un avertissement « Release notes ».

**Ne corrigez pas les notes d'une version déjà sortie.** Toute modification du fichier compte comme les notes d'une nouvelle version : la carte les remontrerait.

## Format

```json
{
  "ajouts": [
    {"fr": "Onglets sur le côté : en panneau ou en colonne compacte.", "en": "Tabs on the side: as a panel or a compact column."}
  ],
  "corrections": [
    {"fr": "Le menu des modèles n’est plus coupé par la bulle.", "en": "The model menu is no longer cut off by the bubble."}
  ]
}
```

- `ajouts` remplit la section « Ajouts » (ce qui est nouveau). `corrections` remplit la section « Corrections » (ce qui est réparé).
- Chaque ligne est écrite en français (`fr`) et en anglais (`en`).
- Une section vide (`[]`) n'est pas affichée. Si les deux sont vides, la sortie n'a pas de notes.

## Limites

- **10 lignes au maximum par section.** Mettez le plus important en premier. Pour une sortie normale, visez 2 ou 3, comme la maquette Paper (p-L-0, 142E-0 et 144O-0).
- **80 caractères au maximum par ligne et par langue**, soit deux lignes de la carte. Visez 70 ou moins.
- La petite carte ne montre que le haut. « Voir les N nouveautés » affiche le reste.
- Utilisez l'apostrophe ’, pas '. Pas d'espace en trop, pas de retour à la ligne.

## Le ton

- Décrivez ce que la personne voit ou peut faire, pas le travail technique.
- Pas de numéro de ticket, de nom de branche ni de mot technique (iframe, WebView, uBlock…).
- Des phrases courtes et simples, au présent, qui finissent par un point. Regroupez ce qui va ensemble : « Nouvelles fenêtres : impression avec aperçu, pages d’erreur, onglet planté. »

## Voir les notes dans la vraie carte

`powershell -File ./test-release-notes.ps1` vérifie que chaque ligne de chaque version tient sur deux lignes de la carte, en français et en anglais.
Il enregistre aussi des images de la carte dans `build/release-notes/` (`release-full-fr-144.png`, `release-mini-fr-144.png`, et la même chose en anglais).
Ce test ouvre de vraies fenêtres : lancez-le pendant la tournée de nuit ou sur un bureau caché.

## Comment ça marche

- **Les notes par version viennent de l'historique git** de `release-notes/notes.json` sur main. Chaque fusion qui a changé ce fichier donne une version avec ses notes : c'est le numéro de cette fusion (0.3.<nombre de commits>). Personne n'a à recopier les anciennes notes.
  `tools/release/build.py` les compile dans la version (`release-notes-data.h`, généré par `tools/release/release_notes.py`). `src/release-notes.h` l'inclut.
- Le navigateur retient **la version jusqu'à laquelle la personne a vu les notes** (fichier `release-notes` de ses données : `seen=0.3.950`). Après une mise à jour, il montre les notes des versions plus récentes que celle-là, jusqu'à la version installée.
  Les anciens états restent compris : l'identifiant fixe des versions jusqu'à 0.3.840 vaut « vu jusqu'à 0.3.646 », et l'empreinte écrite par les versions 0.3.842 à 0.3.898 vaut la version de ces notes.
- Si l'état n'existe pas, ce qui est plus récent que la version installée avant la mise à jour compte comme nouveau.
- Une première installation n'ouvre pas la carte : elle retient seulement la version installée.
- « À propos › Notes de version » ouvre la carte complète avec les notes de la version la plus récente qui en a. Une version sans notes ouvre la page GitHub de la version.
