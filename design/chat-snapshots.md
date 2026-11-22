# Chat intégré — Annuler les changements (contrat C)

Worker C3. Code : `src/change-snapshots.{h,cpp}`. Tests : `tests/snapshots/`. Pour C4 (branchement navigateur).

## En bref

- Le navigateur garde **ses propres copies** des fichiers du projet. Aucun `git` n'est lancé, le `.git` de l'utilisateur n'est jamais lu ni touché.
- Avant chaque tour de l'agent, on relève le dossier ; après, on le relève encore : la différence donne la liste « créés / modifiés / supprimés » avec les lignes +/−.
- « Tout annuler » remet **seulement les fichiers que le tour a changés**, tels qu'ils étaient avant. Un fichier que l'utilisateur a retouché depuis n'est **jamais écrasé sans demander** : l'appel répond « conflit », nomme les fichiers et ne touche à rien.
- Le contenu est remis **octet pour octet** : encodage, BOM et fins de ligne (CRLF / LF) restent identiques.
- Une coupure (plantage, coupure de courant) au milieu d'une restauration est **reprise au prochain appel**, sans jamais écraser ce que l'utilisateur a changé entre-temps.

## Vocabulaire

| Mot | Sens |
|---|---|
| **Tour** (`turnId`, `trn_…`) | Une exécution de l'agent, entre `BeginTurn` et `EndTurn`. |
| **Point de reprise** (`checkpointId`, `chk_…`) | Le dossier **tel que ce tour l'a laissé**. Chaque tour en a un, même sans changement. |
| **Conflit** | Un fichier n'est plus comme le tour l'a laissé (l'utilisateur l'a modifié, supprimé, recréé…). |
| **Non conservé** | Fichier dont on ne garde pas le contenu (trop gros, secret, illisible). Il apparaît dans la liste s'il change, mais « Annuler » le laisse tel quel. |

## API (`namespace ChangeSnapshots`, `src/change-snapshots.h`)

Sans UI, sans globales, sans travail en arrière-plan. **Toutes les méthodes bloquent** (accès disque) : à appeler **hors du fil UI**. Un objet `Store` sert un projet à la fois ; ses méthodes sont sérialisées par un mutex interne. Plusieurs `Store` (fils ou processus) peuvent viser le même projet.

```cpp
ChangeSnapshots::Store store(dataRoot + L"\\chat-snapshots");   // ne fait aucun accès disque

// Début de tour (avant session/prompt)
std::string turnId; ChangeSnapshots::ScanStats stats;
auto r = store.BeginTurn(projectDir, &turnId, &stats);

// Fin de tour (TurnEnded, annulation, erreur : toujours l'appeler)
ChangeSnapshots::TurnSummary summary;
r = store.EndTurn(&summary);            // summary.changes, .additions, .deletions, .checkpointId

// « Tout annuler » (dernier tour)
r = store.UndoTurn(summary.turnId);     // Refuse par défaut
if (r.status == ChangeSnapshots::Status::Conflict) { /* montrer r.conflicts, puis : */
    r = store.UndoTurn(summary.turnId, ChangeSnapshots::OnConflict::KeepMine);   // ou Overwrite
}

// « Restaurer » un point de reprise : annule tous les tours suivants
r = store.Restore(checkpointId);

// « Voir les changements » d'un fichier pour un tour
ChangeSnapshots::FileDiff diff;
r = store.Diff("src/app.css", checkpointId, &diff);   // diff.unified
```

### Méthodes

| Méthode | Rôle | Erreurs propres |
|---|---|---|
| `Open(projectDir)` | Lie le magasin à un dossier sans démarrer de tour (annuler après un redémarrage). Termine une restauration interrompue et ferme un tour laissé ouvert par un plantage. | `InvalidProject`, `Busy` |
| `BeginTurn(projectDir, &turnId, &stats)` | Relève le dossier avant l'agent. Fait aussi ce que fait `Open`. | `InvalidProject`, `ProjectTooLarge`, `TurnOpen`, `Busy`, `IoError` |
| `EndTurn(&summary, &stats)` | Relève le dossier après l'agent, écrit le tour, applique la purge. | `NoTurnOpen`, `Busy` (réessayer), `ProjectTooLarge` / `IoError` (tour clos **sans** point de reprise : `summary.checkpointId` vide) |
| `UndoTurn(turnId, onConflict)` | Annule ce tour **et tous les suivants** encore appliqués. | `NotFound`, `AlreadyUndone`, `TurnOpen`, `Conflict`, `Partial`, `Corrupt` |
| `Restore(checkpointId, onConflict)` | Revient au dossier tel que ce tour l'a laissé : annule tous les tours **suivants**. `Ok` sans rien faire si c'est déjà l'état courant. | idem |
| `Diff(file, checkpointId, &diff, maxBytes)` | Ce que le tour de ce point de reprise a fait à `file`, en diff unifié. | `NotFound`, `Corrupt` |
| `Turns(&list)` | Tours du projet, du plus ancien au plus récent (annulés inclus, marqués). | |
| `Purge(&usage)` / `GetUsage(&usage)` | Applique les limites maintenant / mesure le magasin. | |
| `Forget()` | Efface toutes les copies du projet lié (les fichiers du projet ne sont pas touchés). | `TurnOpen` |
| `SetLimits(maxTurns, maxStoreBytes)` | Réglages changés ; pris en compte à la purge suivante. | |

`UndoTurn` et `Restore` sont refusés pendant un tour (`TurnOpen`).

### `Result`

```cpp
struct Result {
    Status status;                        // Ok, Conflict, Partial, NoProject, InvalidProject, ProjectTooLarge,
                                          // TurnOpen, NoTurnOpen, NotFound, AlreadyUndone, Busy, Corrupt, IoError
    std::string detail;                   // technique, en anglais, pour les logs (l'UI écrit ses phrases)
    std::vector<Conflict> conflicts;      // { path, reason }
    std::vector<std::string> restored;    // fichiers remis ou retirés
    std::vector<std::string> skipped;     // laissés tels quels (conflits en KeepMine, contenus non conservés)
    std::vector<std::string> failed;      // Partial : n'ont pas pu être écrits
    std::vector<std::string> undoneTurns; // tours maintenant marqués annulés
    bool resumedRestore;                  // une restauration interrompue vient d'être terminée
};
```

`StatusName(status)`, `ChangeKindName(kind)`, `NotKeptName(reason)` et `ConflictReasonName(reason)` donnent des noms stables (`"conflict"`, `"modified"`, `"too-large"`, `"modified-since-turn"`…) pour les messages vers le panneau.

### Conflits

`OnConflict::Refuse` (défaut) : au moindre conflit, `Status::Conflict`, liste dans `conflicts`, **rien n'est touché**, aucun tour n'est marqué annulé. L'UI choisit ensuite :

- `OnConflict::KeepMine` : les autres fichiers sont remis ; ceux en conflit restent tels quels (`skipped`, et toujours nommés dans `conflicts`).
- `OnConflict::Overwrite` : l'ancien contenu est remis même par-dessus les changements de l'utilisateur (choix explicite). Les blocages durs restent laissés tels quels.

| `ConflictReason` | Sens | `Overwrite` passe outre ? |
|---|---|---|
| `ModifiedSinceTurn` | Le fichier n'est plus comme le tour l'a laissé. | oui |
| `DeletedSinceTurn` | Le tour avait laissé un fichier ; il a disparu. | oui (il est recréé) |
| `CreatedSinceTurn` | Le tour n'avait rien laissé là ; un fichier s'y trouve. | oui (il est remplacé / retiré) |
| `EditedBetweenTurns` | Modifié entre deux des tours annulés, puis re-modifié par un tour suivant. | oui |
| `InUse` | Lecture seule, ou tenu ouvert par un autre programme. | tenté ; sinon `failed` |
| `Blocked` | Un dossier se trouve à la place du fichier, ou un lien sur le chemin. | non |
| `SnapshotLost` | Le contenu à remettre manque ou est abîmé dans le magasin. | non |

Un fichier **déjà** dans l'état voulu (l'utilisateur l'a remis lui-même) n'est pas un conflit : il est ignoré.

Garde-fou supplémentaire : juste avant d'écrire chaque fichier, sa taille et sa date sont re-vérifiées ; s'il a bougé depuis la vérification (une sauvegarde qui passe entre les deux), il est laissé et nommé dans `conflicts`.

### `TurnSummary` et `FileChange`

```cpp
struct TurnSummary {
    std::string turnId, checkpointId;
    std::uint64_t sequence;               // 1, 2, 3… par projet
    std::int64_t startedAt, endedAt;      // secondes Unix
    bool interrupted;                     // clos par la reprise après plantage : peut contenir des
                                          // modifications de l'utilisateur. Prévenir avant d'annuler.
    bool undone;
    std::vector<FileChange> changes;      // triés par chemin
    int additions, deletions;             // totaux
};
struct FileChange {
    std::string path;                     // UTF-8, relatif au projet, séparateur '/'
    ChangeKind kind;                      // Created | Modified | Deleted
    bool binary;                          // pas du texte : additions = deletions = 0
    int additions, deletions;             // lignes
    bool restorable;                      // false : le contenu d'avant n'a pas été conservé
    NotKept notKept;                      // No | TooLarge | Secret | Unreadable
    std::string renamedFrom, renamedTo;   // renommage exact (même contenu) : indication pour l'UI
};
```

Un renommage est toujours **un fichier supprimé + un fichier créé** ; quand le contenu est strictement identique, `renamedFrom` / `renamedTo` les relient pour que l'UI puisse écrire « renommé ».

### `FileDiff`

```cpp
struct FileDiff {
    std::string path; ChangeKind kind; bool binary; bool restorable; NotKept notKept;
    int additions, deletions;
    std::string unified;        // UTF-8, lignes terminées par "\n", 3 lignes de contexte
    bool truncated;             // coupé à maxBytes (défaut 512 Ko), sur une limite de ligne
    std::string encodingBefore, encodingAfter;  // "utf-8", "utf-8-bom", "utf-16le", "utf-16be", "windows-1252", "binary", ""
    bool lineEndingsChanged;    // CRLF <-> LF
};
```

Format du texte :

```
--- a/src/app.css            (ou --- /dev/null pour un fichier créé)
+++ b/src/app.css            (ou +++ /dev/null pour un fichier supprimé)
@@ -12,7 +12,8 @@
 ligne inchangée
-ligne retirée
+ligne ajoutée
\ No newline at end of file
```

`unified` est vide pour un fichier binaire ou non conservé. Les lignes sont affichées sans leur fin de ligne ; le panneau doit **échapper le HTML** (le contenu vient des fichiers du projet).

### Fonctions pures (sans accès disque)

- `DiffContents(path, &before, &after, wantUnified, maxBytes)` : le même diff sur deux contenus en mémoire.
- `GitignoreIgnores(texte, chemin, estDossier)` : un `.gitignore` à la racine ignore-t-il ce chemin ?
- `MayHoldSecrets(nomDeFichier)`, `ProjectKey(cheminCanonique)`.

## Correspondance avec le contrat B (panneau)

| Message | Appel |
|---|---|
| début d'un `send` | `BeginTurn(projectDir)` — si `ProjectTooLarge` : lancer l'agent quand même, sans « Annuler » |
| `turn.end` | `EndTurn(&summary)` → fichiers, +/−, `checkpointId` |
| `undo.turn` | `UndoTurn(turnId)` ; sur `Conflict` : demander, puis `KeepMine` ou `Overwrite` |
| `checkpoint.restore` | `Restore(checkpointId)` |
| `checkpoint.viewChanges` | `Diff(file, checkpointId)` pour chaque fichier de `summary.changes` |
| ouverture d'un historique | `Open(projectDir)` puis `Turns()` (savoir quels tours existent encore, lesquels sont annulés) |

À savoir pour C4 :

- Les identifiants de tour sont à garder avec l'historique du chat. Un tour purgé répond `NotFound` : masquer « Annuler » / « Restaurer » pour lui.
- Pour revenir **avant le tout premier tour**, utiliser `UndoTurn(premierTour)` (il n'existe pas de point de reprise « zéro »).
- Un seul tour à la fois par projet. Un deuxième onglet sur le même dossier reçoit `TurnOpen` : mettre son tour en file.
- Si l'objet `Store` est détruit (ou le navigateur tué) pendant un tour, le tour est clos à la prochaine ouverture, avec `interrupted = true`.
- Pas de « Rétablir » en V1 : un tour annulé ne peut pas être ré-appliqué (son contenu reste dans le magasin jusqu'à la purge, donc faisable plus tard).

## Ce qui est relevé

Le dossier du projet, récursivement, **sauf** :

- les dossiers `node_modules`, `.git`, `dist`, `build`, `.next`, `.astro`, `.turbo`, `.svelte-kit`, `.nuxt`, `.output`, `.vercel`, `.cache`, `.parcel-cache`, `.vite`, `.hg`, `.svn`, à n'importe quelle profondeur (`Options::excludedDirectories`) ; `.git` est exclu **quelles que soient les options** ;
- les noms que Win32 ne sait pas écrire (caractères de contrôle, point ou espace final) ;
- ce que les `.gitignore` ignorent (racine et sous-dossiers) ;
- les liens symboliques, jonctions et fichiers « en ligne seulement » (OneDrive…) : jamais suivis, jamais lus ;
- le magasin lui-même s'il se trouve dans le projet ;
- nos fichiers temporaires `*.snaptmp`.

**Non conservés** (listés s'ils changent, jamais copiés, jamais restaurés) :

- fichiers de plus de **5 Mo** (`Options::maxFileBytes`) ;
- fichiers pouvant contenir des secrets : `.env`, `.env.*` (sauf `.example` / `.sample` / `.template`), `*.env`, `.envrc`, `.dev.vars`, `*.pem`, `*.key`, `*.p8`, `*.p12`, `*.pfx`, `*.jks`, `*.keystore`, `*.ppk`, `*.tfvars`, `*.tfstate`, `id_rsa*`, `id_dsa*`, `id_ecdsa*`, `id_ed25519*`, `.npmrc`, `.netrc`, `.pypirc`, `.git-credentials`, `.pgpass`, `credentials.json`, `secrets.json|yaml|yml`. Liste écrite pour ce projet (`Options::skipSecretFiles`) ;
- fichiers illisibles au moment du relevé (verrouillés par un autre programme).

### `.gitignore` pris en charge

Lignes vides, commentaires `#`, négation `!`, `/` final (dossiers seulement), `/` initial ou intérieur (ancré au dossier du `.gitignore`), `*`, `?`, `[a-z]`, `**/`, `/**`, `/**/`, échappement `\`. La comparaison ignore la casse (comme git sous Windows). Le `.gitignore` le plus profond gagne ; dans un fichier, la dernière ligne qui correspond gagne. Un fichier sous un dossier ignoré est ignoré (impossible de le ré-inclure).

Non pris en charge : `.git/info/exclude`, le fichier d'exclusions global de git, `core.excludesFile`.

## Format du stockage

`<données>\chat-snapshots\<projectKey>\` — `projectKey` = nom du dossier en minuscules (`[a-z0-9-]`, 32 caractères au plus) + `-` + 16 chiffres hexadécimaux du SHA-256 du chemin complet en minuscules. Exemple : `mon-site-1f2e3d4c5b6a7988`.

| Fichier | Contenu |
|---|---|
| `project.json` | `{"schema":"browser-chat-snapshots/1","root":"C:\\dev\\mon-site","createdAt":…}` |
| `index` | Dernier état connu du dossier. Texte : 1re ligne `browser-chat-snapshot-index/1`, puis `D\t<dossier>` et `F\t<sha256 ou L/S/U>\t<taille>\t<date FILETIME>\t<0/1 à relire>\t<chemin>`. Illisible ⇒ ignoré, tout est relu (rien n'est perdu). |
| `blobs\<aa>\<62 hex>` | Contenu brut d'un fichier, nommé par son SHA-256. Un même contenu n'est stocké qu'une fois. Vérifié à chaque relecture. |
| `turns\<seq sur 8 chiffres>-<turnId>.json` | Un tour : `schema` `browser-chat-snapshot-turn/1`, `id`, `checkpoint`, `seq`, `startedAt`, `endedAt`, `interrupted`, `undone`, `changes[]` (`path`, `kind`, `before`, `after`, `binary`, `additions`, `deletions`, `renamedFrom?`, `renamedTo?`), `createdDirs[]`. |
| `open-turn.json` + `turn.lock` | Un tour est en cours. Le verrou est tenu par le processus pendant le tour ; s'il a disparu mais que le marqueur reste, le tour a été interrompu. |
| `restore.json` | Journal d'une restauration en cours (`browser-chat-snapshot-restore/1`) : tours concernés, dossiers à retirer, et par fichier la cible, ce qui s'y trouvait, et le nom du fichier temporaire. |
| `lock` | Verrou court autour de chaque opération (2 s d'attente, puis `Busy`). |

État d'un fichier dans un tour (`before` / `after`) : `null` (absent), `{"hash":"…","size":n}` (contenu conservé) ou `{"notKept":"too-large|secret|unreadable","size":n,"mtime":t}`.

Seules les **différences** sont gardées par tour (pas une copie complète du dossier à chaque fois). Le magasin contient donc : une copie de l'état courant du projet + les contenus avant/après des fichiers changés par les tours gardés.

### Écritures

- Fichiers du magasin : fichier temporaire + `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`.
- Fichiers du projet (restauration) : fichier temporaire `~<id>-<n>.snaptmp` **dans le même dossier**, vidé sur disque, puis `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`. Un lecteur ne voit jamais un fichier à moitié écrit.
- Ordre d'une restauration : journal → suppressions → dossiers créés par les tours retirés s'ils sont vides → écritures → tours marqués annulés → dossier relevé à nouveau → journal effacé.

### Reprise après coupure

Au prochain `Open` / `BeginTurn` / `UndoTurn` / `Restore`, si `restore.json` existe, chaque fichier du journal est examiné :

- déjà dans l'état voulu → rien à faire ;
- encore tel qu'il était au moment de la décision (taille, date **et** contenu) → la restauration est appliquée ;
- autre chose → l'utilisateur y a touché depuis : laissé tel quel, nommé dans `conflicts`.

Les fichiers temporaires du journal sont effacés. `Result::resumedRestore` vaut `true`.

Coupure pendant `EndTurn` ou pendant un tour : le tour est refermé à la prochaine ouverture (`interrupted = true`).

### Purge

À chaque `EndTurn` (et sur `Purge()`) :

1. on garde les **N derniers tours qui ont changé quelque chose** (`maxTurns`, défaut **50**) et tous les tours venus après le (N+1)-ième ; les tours sans changement ne comptent pas et restent restaurables tant que le tour qui les précède est gardé (plafond dur : 8 × N enregistrements) ;
2. les contenus auxquels plus rien ne se réfère sont effacés ;
3. si le magasin du projet dépasse `maxStoreBytes` (défaut **500 Mo**), les tours les plus anciens sont retirés jusqu'à repasser dessous. Le dernier tour avec changements est toujours gardé.

`Store::SetLimits` ou `Options` pour changer ces valeurs.

## Performance

Objectif du contrat : relever un projet de 5 000 fichiers en **moins de 300 ms à chaud**, hors fil UI. **Mesuré : 45 ms** (médiane ; maximum 49 ms sur 15 passes).

Mesures de `tests/snapshots/snapshot_bench.cpp` (17 novembre 2026, machine de développement, 16 processeurs logiques, SSD, antivirus Windows actif). Projet généré : 5 011 fichiers conservés dans 250 dossiers (12,4 Mo, 11 `.gitignore`), plus 3 700 fichiers à ignorer (`node_modules`, `dist`, `coverage`, `*.log`).

| Opération | Temps |
|---|---|
| Premier `BeginTurn` (tout lire, tout copier : 5 002 contenus) | 3,2 s |
| **`BeginTurn` à chaud, rien de changé** (nouvel objet `Store`, index relu du disque) | **45 ms** médiane (41 à 49 ms) |
| — dont lister les dossiers et comparer | 34 ms |
| `BeginTurn` à chaud, même objet `Store` | 44 ms médiane |
| `EndTurn`, rien de changé (relevé + purge) | 92 ms médiane |
| `EndTurn`, 13 fichiers changés (10 modifiés, 2 créés, 1 supprimé) | 111 ms |
| `Diff` des 13 fichiers | 47 ms |
| `UndoTurn` des 13 fichiers (écritures vidées sur disque) | 85 ms |
| `Purge` (liste les 5 014 contenus) | 38 ms |

« À chaud » = le projet a déjà été relevé une fois et ses fichiers sont dans le cache disque de Windows. Le relevé à chaud ne lit aucun fichier du projet (`ScanStats::read == 0`, vérifié par le test). Refaire la mesure : `python -I tests/snapshots/run_tests.py` (échoue au-delà du budget ; `--files N`, `--budget-ms N`).

Principe : le relevé liste les dossiers (une seule requête par dossier, taille et date comprises) et ne **lit** un fichier que si sa taille ou sa date a changé depuis le dernier relevé. Un fichier dont la date est à moins de 2 s du relevé est relu la fois suivante (une modification dans la même « tranche » d'horloge ne peut pas passer inaperçue). Le premier relevé d'un projet copie tout : il est plus long (plusieurs fils, jusqu'à 8).

## Compilation et tests

- À lier dans `browser.exe` : `src/change-snapshots.cpp` (déjà dans `CHAT_SOURCES` de `tools/release/build.py`) ; bibliothèque : `-lbcrypt` (déjà dans `BROWSER_LIBS`). En-tête à inclure : `change-snapshots.h` (ne tire pas `windows.h`).
- Tests : `python -I tests/snapshots/run_tests.py` (`--quick` sans les mesures). Compilés avec `-Wall -Wextra -Werror`. Tout se passe dans `%TEMP%\browser-snapshots-test-<pid>`, effacé à la fin ; les vraies configurations Claude / Codex sont contrôlées avant et après.
- `Options::fault` et `Options::clock` ne servent qu'aux tests (simuler une coupure à un point précis).

## Limites connues

- **Premier relevé** d'un gros projet : quelques secondes (copie de tous les fichiers conservés). Il a lieu au premier tour. C4 peut l'anticiper en appelant `BeginTurn` puis `EndTurn` à l'ouverture du panneau, hors fil UI.
- **Dossier du projet disparu** (déplacé, supprimé) : `UndoTurn` / `Restore` répondent `InvalidProject` et ne recréent rien ; `Turns` reste lisible.
- **Place disque** : le magasin contient une copie de l'état courant du projet, non compressée. Un projet dont les fichiers conservés dépassent `maxStoreBytes`, ou qui compte plus de 100 000 fichiers relevés (`maxFiles`), répond `ProjectTooLarge` : pas de copie, pas d'« Annuler » (ajouter les dossiers générés au `.gitignore` règle en général le problème).
- **Dossiers vides** : non suivis. Un dossier créé par un tour est retiré à l'annulation s'il est vide ; un dossier vide supprimé par un tour n'est pas recréé.
- **Attributs et droits** : un fichier restauré est un nouveau fichier (date = maintenant, droits hérités du dossier, attributs « caché » / flux NTFS non repris). Le contenu, lui, est exact.
- **Fichier modifié sans changer ni taille ni date** (rare, certains outils le font) : non détecté, comme avec git.
- **Renommage ne changeant que la casse** d'un *dossier* : vu comme suppressions + créations ; l'annulation remet les fichiers mais peut laisser la casse du dossier.
- **Diff** : exact jusqu'à 2 048 lignes changées par fichier (et un budget de calcul) ; au-delà, la zone changée est montrée « tout retiré, tout ajouté » (diff toujours juste, compteurs plus grands). Les compteurs de `EndTurn` et ceux de `Diff` viennent du même calcul.
- **Encodages** : UTF-8 (avec ou sans BOM), UTF-16 avec BOM ; sinon lu comme Windows-1252 pour l'**affichage** seulement. Un octet nul ⇒ binaire. UTF-16 sans BOM et UTF-32 ⇒ binaire.
- **Tour interrompu** (`interrupted`) : ses changements sont « tout ce qui différait à la réouverture », donc possiblement des modifications de l'utilisateur. À signaler avant d'annuler.
- **Deux agents en même temps** sur le même dossier : impossible à démêler ; `TurnOpen` l'interdit.
- **Programmes externes** (serveur de dev, formateur) qui écrivent pendant un tour : leurs écritures sont attribuées au tour. Les dossiers de sortie habituels sont exclus pour cela.
- `Overwrite` ne garde pas de copie de la version de l'utilisateur qu'il remplace (c'est son choix explicite).

## Licence

Aucun code de Gui n'a été repris : seul le comportement de `src/main/checkpoints.ts` a servi de référence (annuler seulement les fichiers changés par les tours, ne pas écraser les modifications de l'utilisateur, exclure secrets et gros fichiers). Rien à ajouter dans `third_party/gui/`. SHA-256 : `bcrypt` de Windows. JSON : `src/strict-json.h` (maison).
