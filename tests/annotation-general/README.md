# Test général du mode annotation

Une seule entrée pour tout le mode annotation (bulle, chat, menus, autorisations, sélection, local / en ligne, pont MCP,
agents). Le plan et les règles : `design/annotation-plan-2026-11-24.md`, §4.

```
python tests/annotation-general/run.py --day               # partie sans fenêtre, faisable en journée (≈ 15 min)
python tests/annotation-general/run.py --night --build     # tout, en vraie fenêtre : tournée de nuit (≈ 2 h)
python tests/annotation-general/run.py --list              # la liste des suites, rien n'est lancé
python tests/annotation-general/run.py --check             # vérifie manifest.json et coverage.json, rien n'est lancé
python tests/annotation-general/run.py --day --only chat-units,chat-panel   # pour chercher un défaut : jamais valable
```

Prérequis : `setup.ps1` a été lancé dans ce dossier de travail (`tools/w64devkit`, `deps/`). Pour `--night` : session
Windows ouverte, écran non verrouillé, personne au clavier, aucune autre suite en vraie fenêtre en cours.

Résultats : `build/annotation-general/day/` ou `night/` — `report.md` (à lire), `report.json`, `logs/<suite>.log`.

| Code de sortie | Sens |
|---|---|
| 0 | VERT : toutes les suites de la partie ont tourné et réussi |
| 1 | ROUGE : au moins une suite en échec, hors délai, pas lancée, ou une règle ci-dessous non tenue |
| 2 | pas lancé : session verrouillée, compilateur absent, manifeste ou table de couverture incorrects |
| 3 | PARTIEL (`--only`) : ne vaut jamais pour le test général, même si tout ce qui a tourné a réussi |

## Les fichiers

- `manifest.json` — la liste des suites, dans l'ordre. `part` : `day` (aucune fenêtre à l'écran) ou `night` (vraies
  fenêtres). `nightCmd` : la commande complète quand la version de jour saute quelque chose (`daySkips` dit quoi).
  `thresholds` : les seuils, `bloquant` ou `objectif`, chacun avec la décision qui le fixe. `lotsDone` : les lots finis.
- `coverage.json` — chaque ligne de l'inventaire (C1 … F11, R1 … R48, R39b, O1 … O12, #129, #130) pointe vers ses tests
  (`suite` = un id du manifeste, `what` = le test dans cette suite), ou porte un `gap` : le lot qui écrira le test, et pourquoi.
- `run.py` — le lanceur.

## Les règles que le lanceur applique

1. Une seule liste, lancée en entier et dans l'ordre.
2. Vert = tout a tourné et tout a réussi. Absent, filtré, hors délai = rouge.
3. Rien n'est relancé.
4. Le rapport porte le commit, la version et l'état de l'arbre. « Valable pour une sortie » = passage de nuit complet,
   vert, sur un arbre propre.
5. Une ligne de l'inventaire sans test = rouge, sauf trou prévu pour un lot pas encore fini. Quand un lot passe dans
   `lotsDone`, ses trous deviennent des échecs.
6. Un seuil `objectif` (ouverture < 150 ms, suivi ± 2 px : décision 5 du 24/11) est mesuré et écrit dans le rapport, il
   ne fait pas échouer. Les suites le disent avec `chat_qa.objective()` ; un objectif que le manifeste ne nomme pas = rouge.
7. Le rapport liste les fichiers de test changés depuis le dernier vert.
8. Sécurité : chaque suite tourne dans un Job Object (`KILL_ON_JOB_CLOSE`), créée suspendue pour que rien ne s'en
   échappe ; un processus encore vivant 10 s après la fin d'une suite = échec, puis il est arrêté. Un `browser.exe`
   lancé d'ailleurs que `build/` ou `%TEMP%` (le navigateur installé, donc le profil réel) = échec immédiat. Les
   configurations réelles de Claude et de Codex sont comparées avant et après.

## Ce que la partie de jour ne fait pas

Elle saute, et le dit dans son rapport : les contrôles en fenêtre réelle du harnais natif (`annotations-ui`), la
connexion d'un agent en console visible et les vrais téléchargements de Node.js (`acp`). La nuit les rejoue en entier.

## Ajouter un test

1. L'écrire dans une suite existante, ou ajouter une suite à `manifest.json` (id, titre, partie, commande, délai).
2. Pointer la ligne de l'inventaire vers lui dans `coverage.json` et retirer son `gap`.
3. `python tests/annotation-general/run.py --check`.

## Hors de ce test

`tests/update-integration/run_all.py` (installateur : fenêtres réelles, tournée de nuit à part),
`test-password-vault.ps1`, `tests/landing/`.
