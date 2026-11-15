# Langue de l'interface — maquettes Paper (à valider)

Document de design uniquement. Aucun code i18n n'est écrit : l'implémentation attend le feu vert explicite de l'utilisateur.

- Fichier : « Browser Design » — https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC
- Page créée : **« Langues — proposition à valider »** — https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-K-0
- Base : `main` d060366 (0.3.294), tiroir Réglages livré (`src/settings-panel.h`).

## Ce qui est proposé

Une seule application, deux langues. Le français reste la langue par défaut.

- Nouvelle rangée **« Langue » / « Language »** dans le premier groupe de l'écran principal des Réglages, sous Profils. Icône Phosphor Bold « translate » 16 (chemin officiel du paquet `@phosphor-icons/core`, le même que celui d'où viennent les autres glyphes ; la page Icon n'est pas touchée).
- La valeur affichée est le nom de la langue dans cette langue : « Français », « English », quelle que soit la langue de l'interface. Le caret vers le bas est celui des rangées à choix (Page d'accueil).
- Clic : menu natif à deux choix (Français, English), coche sur la langue enregistrée, survol et focus existants. Pas de description, pas de « Plus d'infos », pas de nouvelle modale.
- Le choix vaut pour tout le navigateur (tous les profils).
- Le changement s'applique au prochain démarrage. Juste après le choix, la valeur et la coche montrent la langue enregistrée et une note apparaît sous le libellé : « Au prochain démarrage. » (interface française) ou « Applies at next start. » (interface anglaise). La note reste tant que l'interface affiche l'autre langue. L'interface ne bascule jamais à moitié.
- Si l'enregistrement échoue : la valeur reste l'ancienne et une note d'erreur apparaît, « Impossible d'enregistrer la langue. » / « Couldn't save the language. », dans la couleur danger du champ « adresse invalide ». Le texte suffit sans la couleur.
- Recherche dans les réglages : « langue », « language », « anglais », « english », « français » trouvent la rangée. Le titre et le chemin sont dans la langue affichée (« Langue · Réglages », « Language · Settings »).
- Ne changent pas de langue : les noms de profils (« Personnel » est une donnée de l'utilisateur), les noms de dossiers, le contenu des sites.

## Planches (page « Langues — proposition à valider »)

Ligne du haut, en français ; ligne du bas, les mêmes états en anglais. Toutes font 332 × 731, comme le tiroir.

| Planche | État |
|---|---|
| FR 1 · Réglages — Langue (français, par défaut) | écran principal à jour (Profils, Langue, Annotations et agents), pied « browser 0.3.294 » |
| FR 2 · Menu Langue ouvert (survol English) | menu à deux choix, coche sur Français |
| FR 3 · English choisi — au prochain démarrage | valeur English, note « Au prochain démarrage. » |
| FR 3b · Menu rouvert — English coché | interface encore en français, coche sur English, note visible au-dessus du menu |
| FR 4 · Échec d'enregistrement | valeur Français inchangée, note d'erreur |
| FR 5 · Recherche « english » → Langue | correspondance par mot-clé, titre et chemin en français |
| EN 1 · Settings — Language (English) | Settings, Search settings, Search engine, Homepage, Profiles, Language, Tabs, Privacy, Downloads, Display and sound, Annotations and agents |
| EN 2 · Language menu open (hover Français) | même ordre des choix, coche sur English |
| EN 3 · Français chosen — applies at next start | valeur Français, note « Applies at next start. » |
| EN 4 · Save failed — value unchanged | valeur English inchangée, note d'erreur |
| EN 5 · Search « lang » → Language | correspondance dans le titre, « Lang » en blanc |
| Spéc · Langue de l'interface | les règles ci-dessus, en court |

Captures 2x : `build/browser-languages-paper/after/` (fr1…fr5, fr3b, en1…en5, spec).

## Mesures reprises du code et de la référence

- Les rangées, l'en-tête, la recherche et le pied sont des clones de « 01 · Réglages — racine » (p-1-0, lecture seule). Rangée 32, rayon 6, padding 8/4, gap 8 ; libellé 16/20 à 70 % ; valeur 14/20 à 50 % ; icône 16 dans une fente de 16 × 24.
- Menu : celui de « choix Lecture automatique » (288, rayon 8, padding 4, items rayon 4, survol #FFFFFF14, coche 14 à 75 %). Il est posé 4 DIP sous la rangée, à 12 du bord droit, comme « meet.google.com › choix Caméra ».
- Rangée en attente : 56 = `Layout::TwoLine(1)`, comme Lecture automatique. La note est en 14/18 à 50 %. Dans les planches, elle est alignée sous le libellé (x 44). Aujourd'hui, `ValueRow` la dessine à x 20 (`textLeft`) parce qu'aucune rangée à choix avec icône n'avait de note. À l'implémentation, il faudra prendre `nameLeft` quand la rangée a une icône.
- Recherche : carte #202020, rayon 6, padding 8/12/8/10. Le reste de l'écran est atténué à `--opacity-browser-disabled`, comme dans le code livré.
- Largeurs en anglais : la paire la plus longue, « Annotations and agents » + « No agents », garde plus de 16 DIP avant la valeur. Rien n'est tronqué.

## Préservation (avant / après)

Relevé avant écriture : `build/browser-languages-paper/before/baseline.txt` et ses captures.

| Contrôle | Avant | Après |
|---|---|---|
| Hash des tokens | b37eaa4f | b37eaa4f (aucun token créé ni modifié) |
| p-1-0 référence | 165 planches / 18047 nœuds | 165 / 18047 |
| p-H-0 MCP - Design UI UX | 35 / 1832 | 35 / 1832 |
| p-C-0 Icon | 6 / 31 | 6 / 31 |
| Commentaires | 12 fils, tous résolus | les 12 mêmes fils, statuts inchangés |
| CW5-0 (référence clonée) | 5 enfants | 5 enfants, non modifié |

- p-I-0 et p-J-0 n'ont été ni lues ni écrites. Leurs compteurs n'ont pas été relevés avant ; aucun appel ne les a visées.
- Ordre des pages : Paper insère une nouvelle page après la page active. « Langues — proposition à valider » se trouve donc entre p-1-0 et p-I-0. L'ordre relatif des pages existantes n'a pas changé. Aucun renommage, aucune suppression.
- Tout ce qui a été écrit se trouve sur p-K-0. La seule suppression implicite est un remplacement de nœud à l'intérieur d'une de mes propres copies (le glyphe user-circle, corrigé au chemin officiel exact). Pas de chevauchement : 80 DIP entre les planches, la ligne EN commence à y 851.
- Aucune édition concurrente constatée (hash et compteurs identiques avant et après).

## Limites

- Ce sont des maquettes, pas des captures du navigateur. Les captures natives FR/EN viendront avec l'implémentation.
- Le texte « Applies at next start. » est une proposition. Le même identifiant servira aux notes existantes (Lecture automatique, Cookies tiers, Affichage lissé).
- La valeur « Downloads » en anglais suppose une installation Windows en anglais : c'est le nom du dossier, pas une traduction.
