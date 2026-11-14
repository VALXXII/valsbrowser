# Réglages › « Annotations et agents » (V2-D)

Section du tiroir Réglages pour connecter un agent **plus tard** et régler le mode Annotation. Contrat :
`plan-annotations-mcp-local/design/annotations-v2-brief.md`. Composants, tokens et règles repris tels quels des
sections existantes (Paper « Browser — référence unifiée », planches Réglages, relues en lecture seule) : aucun
nouveau style. Seule pièce nouvelle dans le tiroir : le bouton d'action des rangées d'agent, qui est le bouton
`Raised` du système (`Design::Resolve(ButtonVariant::Raised)`, celui du toast de mise à jour) à la hauteur 24 DIP
des contrôles du tiroir.

| Fichier | Rôle |
| --- | --- |
| `src/settings.h` | Clés `annotationLauncher` (vrai), `annotationConnectPrompt` (vrai), `annotationConnectDeclines` (0..3), `annotationRetentionDays` (7 / 30 / 90, défaut 30) ; `ConnectPromptActive`, `SetConnectPrompt`, `RetentionChoices` ; `Change::AnnotationLauncher / AnnotationConnectPrompt / AnnotationRetention` |
| `src/annotation-settings.h` | Modèle pur : statut et action de chaque agent, « il y a … », résumé de la racine, textes de la confirmation, rappel des raccourcis, logos Claude / OpenAI et marque d'annotation (chemins Paper exacts d'`annotation-kit.h`, vérifiés par test) |
| `src/settings-panel.h` | Vue `View::Annotations`, rangée racine, rangées d'agent, hooks, `ClearAnnotationsSpec(n)`, arbre UIA du tiroir entier (il n'en avait pas), rendu hors écran pour les tests |
| `tests/settings_test.cpp`, `tests/annotation_settings_test.cpp`, `test-annotation-settings.ps1` | Tests (voir plus bas) ; captures dans `build/annotations-v2-qa/settings/` |

## Contenu de la section

Racine : rangée « Annotations et agents » (marque d'annotation, résumé « Claude », « Claude, Codex » ou « Aucun
agent ») après « Affichage et son », seulement quand le mode est compilé (dev / preview) et hors fenêtre privée.

- **Agents** — une rangée par app : logo, nom (« Claude Desktop », « Codex »), statut en dessous, bouton à droite.
  Statuts : « Vérification… », « Non installé » (pas de dossier de configuration : pas de bouton), « Non connecté »
  (+ Connecter), « Connecté » / « Connecté · a lu vos annotations il y a 5 min » (+ Déconnecter si notre entrée est
  dans sa configuration ; connecté par un réglage manuel : pas de bouton), « Connecté à un ancien emplacement du
  navigateur » (+ Reconnecter), « Connexion… » / « Déconnexion… » (même bouton, désactivé, garde le focus), un échec
  (message d'agent-connect en rouge danger, rien n'a été écrit, Connecter reste proposé), une étape restante
  (« Cliquez sur Installer dans Claude »). Seul le bouton est cliquable : la rangée autour est inerte.
  Puis « Autre app ou réglage manuel » (ouvre la modale de connexion sur son repli manuel).
- **Mode Annotation** — « Afficher le lanceur d'annotations » ; « Proposer de connecter un agent » (description :
  « Au lancement du mode Annotation, tant qu'aucun agent n'est connecté. ») : l'interrupteur montre si la modale
  s'ouvrirait encore d'elle-même (`annotationConnectPrompt && annotationConnectDeclines < 3`) ; le réactiver remet
  `annotationConnectDeclines` à 0. Le libellé demandé « … au lancement du mode » ne tient pas sur une ligne de
  16 DIP : la fin passe dans la description, comme pour les autres interrupteurs. Rappel texte : « Ctrl+Maj+A lance
  le mode Annotation, Ctrl+Maj+H ouvre l'historique. »
- **Historique** — « Conserver les terminées » (menu décrit 7 / 30 « Recommandé. » / 90 jours ; « Conserver les
  annotations terminées » ne tient pas à côté de sa valeur, le groupe « Historique » dit de quoi il s'agit) ;
  « Ouvrir le dossier des annotations » (rangée dossier des Téléchargements, chemin en infobulle) ;
  « Effacer toutes les annotations… » (rangée danger, désactivée sans annotation) → confirmation
  `ClearAnnotationsSpec(n)` : « Effacer les 12 annotations ? », « Leurs notes et captures seront supprimées de cet
  ordinateur, même celles qu'un agent traite. Impossible d'annuler. », Annuler (focus initial, Échap, Entrée) /
  « Effacer 12 annotations » (danger).

Lecture des réglages tolérante : clé absente = défaut, valeur hors liste ignorée (`annotation-connect-declines`
0..3, `annotation-retention-days` 7 / 30 / 90, booléens `0` / `1`).

## Branchement dans `main.cpp` (fait)

`annotation-mode.h` lit ces clés par leur nom (`Prefs::Prompt`, `Prefs::Declines`, `Prefs::Retention`) : la modale
de lancement ne s'ouvre que si `annotationConnectPrompt` et `annotationConnectDeclines < 3`, « Plus tard »
incrémente `annotationConnectDeclines` (même plafond que `BrowserSettings::MaxConnectDeclines`). La rétention est
lue à la purge (première utilisation du magasin dans la session). `AnnotationStoreRoot()` est fourni par
`src/annotation-entry.h` (V2-C). Lignes ajoutées :

Dans `SettingChanged` (fait disparaître les trois `-Wswitch`) :

```cpp
case Change::AnnotationLauncher:case Change::AnnotationConnectPrompt:case Change::AnnotationRetention:AnnotationSettingsChanged();break;
```

Dans `CreateSettingsPanel()`, avec les autres hooks :

```cpp
namespace AS=AnnotationSettings;
hooks.annotations=[]{return AnnotationsCompiled()&&AnnotationHistoryAvailable();};   // pas en fenêtre privée
hooks.loadAgents=[](std::function<void(AS::Agents)> done){AnnotationAgentsStatus([done](const AnnotationAgentState& c,const AnnotationAgentState& x){
    auto to=[](const AnnotationAgentState& s){AS::Agent a;a.detected=s.detected;a.configured=s.configured;a.connected=s.connected;a.stalePath=s.stalePath;a.lastReadAt=s.lastReadAt;return a;};
    AS::Agents a;a.claude=to(c);a.codex=to(x);done(a);});};
hooks.connectAgent=[](AS::App app,bool on,std::function<void(bool,std::wstring)> done){
    AnnotationConnectAgent(app==AS::App::Claude?AnnotationApp::Claude:AnnotationApp::Codex,on,[done](bool ok,const std::wstring& m){done(ok,m);});};
hooks.openConnect=[]{settingsDrawer.Hide(false,true);AnnotationOpenConnect(true);};
hooks.annotationFolder=[]{return AnnotationStoreRoot();};
hooks.openAnnotationFolder=[]{AnnotationOpenFolder();};
hooks.countAnnotations=[](std::function<void(std::size_t)> done){AnnotationCount(std::move(done));};
hooks.clearAnnotations=[](std::size_t n,bool keyboard){keyboardModality=keyboard;
    auto spec=NativeChrome::SettingsUI::ClearAnnotationsSpec(n);spec.buttons[1].closes=false;
    spec.onButton=[](int id){if(id!=MD::AcceptButton)return;modal.SetBusy(true,L"Effacement…");
        AnnotationClearAll([](bool ok,std::size_t removed){
            if(!ok){modal.SetBusy(false);modal.SetError(L"Certaines annotations n'ont pas pu être effacées. Réessayez.");}
            else{modal.Finish(MD::AcceptButton);accessibility.Notify('S',(std::to_wstring(removed)+(removed>1?L" annotations effacées":L" annotation effacée")).c_str(),L"Settings");}
            settingsDrawer.AnnotationsChanged();});};
    AskModal(std::move(spec),nullptr,[](const MD::Result&){});};
```

(`MD`, `modal`, `AskModal` sont ceux de `src/elements-integration.h`, comme pour « Effacer les données… ».)
Facultatif : `settingsDrawer.AnnotationsChanged()` après la fermeture de la modale de connexion, pour que le
tiroir rouvert ne montre pas un statut d'un instant plus ancien (il redemande de toute façon à chaque ouverture),
et `settingsDrawer.ShowAnnotations()` pour ouvrir le tiroir directement sur la section.

## Accessibilité

Le tiroir Réglages n'avait pas d'arbre UIA ; il en a un maintenant pour toutes ses vues (`Accessibility::Bridge`) :
titre de la vue (texte), Retour, « Fermer les réglages » (Échap), libellés de groupe et notes (texte), interrupteurs
(Toggle On/Off + description en aide), rangées à valeur (ExpandCollapse + valeur en `ItemStatus`), rangées dossier
(chemin complet en aide), rangées danger (désactivées quand inertes), rangée d'agent lue en une phrase
(« Codex : Non installé ») et son bouton nommé avec l'app (« Connecter Claude Desktop », statut en aide, désactivé
pendant l'action). Invoke / Toggle / Expand sont postés au fil UI (aucun blocage du fournisseur). Ordre Tab = ordre
de lecture des contrôles ; les annonces (« Connexion de Claude Desktop… », « Claude Desktop connecté », l'erreur)
passent par une notification UIA.

## Tests et preuves

`./test-annotation-settings.ps1` :

1. `settings_test` (`-Werror`) : défauts, aller-retour de toutes les valeurs, fichier ancien sans les clés, valeurs
   invalides ignorées (CRLF compris), règle de l'interrupteur (3 « Plus tard » → éteint ; réactivé → compteur à 0).
2. `annotation_settings_test` (101 vérifications) : modèle pur ; tiroir réel (rangées, groupes, libellés qui tiennent
   dans leur boîte, ordre Tab, Tab / Haut, test de clic du bouton seul, nœuds UIA) ; Connecter / Déconnecter Claude et
   Codex par `src/agent-connect.*` **uniquement sur dossiers temporaires** (`APPDATA`, `LOCALAPPDATA`, `USERPROFILE`,
   `CODEX_HOME`, `PATH` redirigés sous `build/annotations-v2-qa/settings/tmp`, arrêt si ce n'est pas le cas) :
   fusion JSON, table TOML ajoutée sans toucher au reste, sauvegardes, retrait de notre seule entrée, état occupé,
   configuration illisible (rien n'est écrit, message en danger) ; interrupteurs, menu de conservation, dossier,
   confirmation d'effacement (nombre nommé, focus sur Annuler, Entrée n'efface pas) ; captures 96 et 144 dpi.
3. Client UI Automation réel hors processus sur la section affichée : noms, rôles, aide, Toggle, ExpandCollapse,
   puis `Toggle()` du lanceur appliqué par le tiroir (`uia-tree.txt`).
4. Parcours réel dans une copie du preview : `python tests/annotations/settings_walk.py` (garde-fous de
   `sandbox_qa_v2.py` : `BROWSER_ANNOTATIONS_TEST=1`, `BROWSER_ANNOTATIONS_CONNECT_ROOT` sous `build/`, navigateur
   et serveur fermés dans un `finally`) : Réglages → section → lanceur masqué puis réaffiché (fichier `settings`,
   journal) → Connecter / Déconnecter Claude Desktop sur la configuration redirigée → `build/annotations-v2-qa/settings-walk/`.
5. Empreinte des **vraies** configurations (`%APPDATA%\Claude\claude_desktop_config.json`,
   `%USERPROFILE%\.codex\config.toml`, nos sauvegardes / temporaires, `Claude Extensions`) avant et après chaque
   étape : toute différence fait échouer.

Captures : `root`, `section`, `claude-connected`, `codex-busy`, `both-connected`, `claude-failed`, `prompt-off`,
`focus-connect` (`-96.png` / `-144.png`), `confirm-clear-96.png` / `-144.png`.
