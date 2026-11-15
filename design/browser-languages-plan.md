# Langues de l'interface — design technique et plan

Mandat : `build/browser-languages/task.md` (approuvé). Design visuel validé : Paper p-K-0 (`design/browser-languages-paper-report.md`). Rapport final : `design/browser-languages-report.md`.

## Architecture

- **Catalogue** `src/i18n-catalog.inc` : une entrée par message, `I18N(id, L"français", L"English")`. Les identifiants sont sémantiques (`settings_title`, `downloads_failed_reason`…). C'est un X-macro, donc une seule source pour l'énumération et la table. Les deux langues sont des littéraux : les pointeurs restent valides pendant toute la vie du processus, sans copie ni durée de vie à gérer.
- **Moteur** `src/i18n.h` (C++17 pur, sans Win32) :
  - `I18n::Locale { Fr, En }`, valeur atomique du processus, Fr par défaut ;
  - `T(Id)` renvoie un `const wchar_t*` ;
  - `Format(Id, {args})` remplace des emplacements nommés `{0}`… : des messages entiers, l'ordre des mots est libre dans chaque langue ;
  - `Plural(idOne, idOther, n, {args})` suit la règle française (0 et 1 au singulier) ou anglaise (seul 1 au singulier) ;
  - `U8(Id)` pour le HTML et le JSON ;
  - `Tag()` renvoie fr-FR / en-US, `HtmlLang()` renvoie fr / en ;
  - formats décimal, taille et pourcentage par langue, sans `setlocale`.
  - Macros `TR(id)`, `TRF(id, …)`, `TRP(id, n, …)`. Le pluriel utilise `id_one` et `id_other`.
- **Pas de figement** : aucune table statique ne garde un texte traduit. Les tables gardent des `Id`, ou bien les fonctions `Info()` rendent des valeurs construites à l'appel. Les clés stockées (`"balanced"`, `"custom"`…), les URL, les identifiants UIA (`automationId`) et les jetons de protocole ne passent jamais par le catalogue.
- **Préférence** `src/ui-language.h` :
  - fichier `<dataDir>\ui-language` qui contient `browser-ui-language 1` puis `language=fr|en` ;
  - `dataDir` est la racine globale, commune à tous les profils (portable : à côté de l'exe ; installé : `<root>\data`) ;
  - écriture atomique avec `ProfileStore::WriteAtomic` ;
  - lecture tolérante : fichier absent, illisible, version inconnue ou valeur invalide donnent le français avec un statut distinct, journalisé ;
  - un échec d'écriture est signalé, jamais un faux « enregistré ».
- **Démarrage** : `wWinMain` lit la préférence juste après `ResolveBrowserPaths()`, avant tout dialogue, fenêtre ou WebView2, et fixe la langue du processus. Elle ne change plus ensuite. Chaque nouvelle fenêtre est un nouveau processus et relit le fichier : la langue s'applique « au prochain démarrage » de chaque fenêtre, jamais à moitié dans une fenêtre ouverte.
- **WebView2** : `EngineOptions::language` vaut fr-FR ou en-US à la création de chaque environnement, y compris pour la sonde de version.
- **Réglages** :
  - rangée « Langue / Language » sous Profils ;
  - icône Phosphor Bold « translate » en chemin vectoriel ;
  - menu natif à deux choix, avec les langues écrites dans leur propre langue ;
  - note « Au prochain démarrage. » tant que la préférence diffère de la langue du processus ;
  - note d'erreur en encre danger si l'écriture échoue ;
  - note à x 44 (`nameLeft`) sous le libellé quand la rangée a une icône ;
  - recherche bilingue par mots-clés.
- **Migration portable → installée** : le helper copie aussi `ui-language` (même règle que `session`, `downloads`…). Le paquet ne contient que la liste blanche du payload, donc le fichier n'est jamais publié. Test dédié.
- **Couverture** : `tools/i18n/check_strings.py` est lancé par `run_all.py`. Il vérifie :
  - ① le catalogue : identifiants uniques, FR et EN non vides, mêmes emplacements `{n}`, paires `_one/_other`, et EN différent de FR sauf marques et autonymes listés ;
  - ② les sources : tout littéral qui ressemble à du texte d'interface hors `TR*` est une erreur, sauf exemption justifiée : `// i18n-exempt: <raison>` sur la ligne, ou un fichier entier listé avec sa raison (outils internes, protocole, journaux).

## Hors traduction (classés, vérifiés par l'outil)

- Journaux `Log(...)` et identifiants : UIA `automationId`, classes de fenêtre, clés de réglages, jetons MCP et JSON, noms d'outils, chemins, URL.
- Marques et noms propres : browser, WebView2, Claude Desktop, Codex, DuckDuckGo…
- Données de l'utilisateur et des sites : noms de profils, titres de pages, annotations écrites, noms de fichiers. Le nom par défaut « Personnel » est déjà stocké ; seul un profil créé ensuite reçoit le nom par défaut de la langue courante.
- Textes du protocole MCP renvoyés aux agents (`mcp-bridge.cpp`, descriptions d'outils) : texte développeur, gardé tel quel.
- Outils internes de revue : bac à sable `browser://debug` (page HTML de démonstration), labo d'animation `--motion-lab`, démonstration du coffre de mots de passe, crochets e2e. Ce sont des pages de QA en français ; les composants de production qu'ils ouvrent sont, eux, traduits.
- uBlock Origin Lite : il gère ses propres langues.

## Plan

1. Socle : `i18n.h`, catalogue, `ui-language.h`, tests unitaires (formats, pluriels, préférence, erreurs), outil de couverture.
2. Démarrage et WebView2 : langue fixée tôt, `EngineOptions::language`, migration du helper avec son test.
3. Rangée Langue dans les Réglages, recherche, UIA, tests.
4. Traduction par zones, avec un commit par zone et les tests de la zone : Réglages et modèle → chrome et menus et main.cpp → modales et dialogues et invites d'origine → téléchargements, historique, favoris, son → profils → erreurs réseau, TLS et pages intégrées → WebView2 et mises à jour → annotations et connexion d'agents.
5. Preuves : captures natives FR/EN, UIA, parcours « préférence EN → nouveau processus EN → retour FR ». Rapport.
