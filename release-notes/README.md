# Notes de version : la carte « Nouveautés »

Après une mise à jour, le navigateur ouvre une fois la carte « Nouveautés » en bas à droite, à la place de la notification « Mise à jour installée ».
La carte montre le contenu de `release-notes/notes.json`, compilé dans la version au moment du build.

## À chaque sortie (à faire par celui qui fusionne dans main)

1. Avant la fusion, **remplacez** le contenu de `release-notes/notes.json` par les nouveautés de cette sortie.
   Ne mettez que ce qui part vraiment. Retirez les lignes d'une branche qui ne sort pas. Ne gardez jamais les notes de la sortie précédente.
2. Vérifiez avec `python tools/release/release_notes.py`. La commande affiche chaque ligne avec sa longueur, puis « OK ».
   Le build (`tools/release/build.py`) refuse lui aussi un fichier qui ne respecte pas les règles ci-dessous.
3. Commitez le fichier dans la branche qui est fusionnée.

Si une sortie ne modifie pas ce fichier, elle n'a pas de carte. Après sa mise à jour, l'utilisateur voit la notification « Mise à jour installée », jamais les anciennes notes.
Le build de GitHub Actions le signale par un avertissement « Release notes ».

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
- Une section vide (`[]`) n'est pas affichée. Si les deux sont vides, il n'y a pas de carte.

## Limites de la carte

- **4 lignes au maximum par section.** La maquette Paper (p-L-0, 142E-0 et 144O-0) en montre 2 et 3 : visez 2 ou 3.
- **80 caractères au maximum par ligne et par langue**, soit deux lignes de la carte. Visez 70 ou moins.
- La petite carte ne montre que le haut. « Voir tout » affiche le reste : mettez le plus important en premier.
- Utilisez l'apostrophe ’, pas '. Pas d'espace en trop, pas de retour à la ligne.

## Le ton

- Décrivez ce que la personne voit ou peut faire, pas le travail technique.
- Pas de numéro de ticket, de nom de branche ni de mot technique (iframe, WebView, uBlock…).
- Des phrases courtes et simples, au présent, qui finissent par un point. Exemple : « Le menu des modèles n’est plus coupé par la bulle. »

## Voir les notes dans la vraie carte

`powershell -File ./test-release-notes.ps1` vérifie que chaque ligne tient sur deux lignes de la carte, en français et en anglais.
Il enregistre aussi des images dans `build/release-notes/` (`release-full-fr-144.png`, `release-mini-fr-144.png`, et la même chose en anglais).
Ce test ouvre de vraies fenêtres : lancez-le pendant la tournée de nuit ou sur un bureau caché.

## Comment ça marche

- `tools/release/build.py` vérifie le fichier et génère `release-notes-data.h` dans `<out>-obj/gen`. `src/release-notes.h` l'inclut.
- Chaque texte a un identifiant, une empreinte du texte (`notes-…`). Le navigateur retient le dernier identifiant vu dans le fichier `release-notes` de ses données.
  Un nouveau texte ouvre la carte une fois après la mise à jour. Un texte inchangé ne l'ouvre pas.
- « À propos › Notes de version » ouvre la carte complète. Une version sans notes ouvre la page GitHub de la version.
