# Mode Annotation — UI native (V2)

Composants natifs du mode Annotation, refaits sur le design final de Paper (fichier `01M6Q7YT14ZKMGET54ASPNGQEC`, page `p-H-0` « MCP - Design UI UX », relu le 14 novembre 2026 avec `get_jsx` / `get_computed_styles` / `get_screenshot`, rien modifié). Contrat : `plan-annotations-mcp-local/design/annotations-v2-brief.md`. Skills appliqués : apple-design, better-ui / accessibility / typography / colors / layout / writing, emil-design-eng.

| Fichier | Rôle | Planches |
| --- | --- | --- |
| `src/annotation-ui.h` | Include unique pour l'hôte | — |
| `src/annotation-motion.h` | Modèle de mouvement pur : `Bezier` (cubic-bezier CSS exacte), `Ramp` (interruptible, délai), `Presence` (opacité + échelle 0,96→1 / →0,98), `Crossfade` (ressort 0,3 s), `Spec` (toutes les durées) | Spéc ×4 |
| `src/annotation-kit.h` | `Ink` (D2D/DirectWrite/WIC partagé, Inter Display + JetBrains Mono, glyphes Phosphor + logos Claude / OpenAI exacts, icône en trait), `Layer`, `Animator`, pièces communes (`Pieces`, `Logo`, `CloseButton`, `RaisedFill`, `Posed`, clip arrondi) | — |
| `src/annotation-banner.h` | `Banner` (+ BETA, + rappel « Aucun agent connecté · Connecter »), `PaintToolbarButton`, `ToolbarNode` | 1.1, 1.8 |
| `src/annotation-connect.h` | `ConnectModal` (voile + modale + pastille « Reste sur cet ordinateur ») + textes JSON/TOML purs | 1.1 → 1.7 |
| `src/annotation-note.h` | `NotePopover` (panneau compact + pastille « Ce que l'agent verra ») | 2.1 → 2.3 |
| `src/annotation-launcher.h` | `Launcher` (lanceur tiroir + compteur, cartes) ; possède l'historique | 3.1 → 3.3 |
| `src/annotation-history.h` | `HistoryPanel` (liste, filtres, sections par site, détail, vide, menu « … », toast « Annuler ») | 4.1 → 4.3 |
| `src/annotation-onboarding.h` | V1, **déprécié** (remplacé par `ConnectModal`) ; gardé pour que `annotation-mode.h` compile jusqu'à l'intégration V2 | — |
| `tests/annotations-ui/` | Harnais natif (`annotations_ui_test.cpp`, `run.py`, `tables.py`) | toutes |

Aucune E/S disque (hors lecture des polices embarquées), aucune dépendance au magasin : structures simples en entrée, callbacks en sortie. Tout est créé paresseusement : `Init` ne stocke que le contexte (vérifié : ni `Ink`, ni `Animator`, ni fenêtre après les quatre `Init`).

## API d'intégration (pour l'intégrateur, `src/annotation-mode.h`)

```cpp
#include "annotation-ui.h"
using namespace NativeChrome::Annotations;
```

### Bandeau (1.1, 1.8)

```cpp
struct BannerHooks { std::function<void()> exit, connect; std::function<void(const std::wstring&)> announce; };
class Banner {
  void Init(HINSTANCE, HWND browser, std::wstring appRoot, BannerHooks);
  bool Show(RECT bandClientPx, bool reducedMotion);   // fondu 120 ms ; annonce « Mode Annotation activé »
  void Move(RECT); void Hide(bool animate = true);
  void SetReminder(bool on, bool animate = true);    // après « Plus tard » : « Aucun agent connecté · Connecter » → hooks.connect
  void SetBeta(bool);                                // pastille BETA (défaut : oui)
  bool Visible() const; bool ReminderVisible() const; HWND Window() const;
};
```
Clavier : Tab alterne rappel ↔ « Quitter », Entrée/Espace active, Échap = `exit`.

### Modale « Connecter un agent » (1.1 → 1.7)

```cpp
enum class AgentKind { Claude, Codex, Other };
enum class ConnectState { Idle, Working, Connected, Failed, Reconnect };
struct AppStatus { ConnectState state; std::wstring detail; };   // Working: indication ; Failed: message ; Connected: note (« Redémarrez Codex pour l'activer »)
struct ConnectSetup {
  bool launch = true;                 // ouverte à l'activation du mode : la croix et Échap valent « Plus tard » (ConnectEnd::Later)
  std::wstring command;               // chemin réel du bridge (réglage manuel)
  std::vector<std::wstring> args;     // ex. {L"--store", L"C:\\…"}
  std::wstring serverName = L"browser-annotations";
  std::wstring removableDrive;        // L"E:" si GetDriveType == DRIVE_REMOVABLE (1.6)
  AppStatus claude, codex;            // état initial (détection par les dossiers de config / readers\)
};
enum class ConnectEnd { Later, Done, Closed };
struct ConnectHooks {
  std::function<void(AgentKind)> connect;           // clic explicite SEULEMENT ; l'hôte configure hors fil UI puis SetStatus()
  std::function<void(const std::wstring&)> copy;    // l'hôte écrit le presse-papiers (commande, JSON, TOML)
  std::function<void(ConnectEnd)> closed;           // Later = croix/Échap à l'ouverture (compteur de refus côté hôte), Done = « Terminé », Closed = croix ensuite
  std::function<void(const std::wstring&)> announce;
};
class ConnectModal {
  void Init(HINSTANCE, HWND browser, std::wstring appRoot, ConnectHooks);
  bool Show(const ConnectSetup&, RECT browserClientScreenPx, bool reducedMotion, int delayMs = 0); // delayMs = 80 après le bandeau (Spéc Lancement)
  void SetStatus(AgentKind, const AppStatus&);       // Working → Connected / Failed : fondu croisé du bouton + annonces
  void SetRemovable(const std::wstring& drive);
  void Close(bool animate = true, ConnectEnd = ConnectEnd::Closed);
  bool Visible() const; HWND Window() const; HWND Veil() const;
};
std::wstring ConnectJson(const ConnectSetup&), ConnectToml(const ConnectSetup&);   // textes purs (testés)
```
**Révision Paper du 14 novembre 2026 (1.1, 10OW-0)** : la description « Il recevra vos annotations dès la première. » et le bouton « Plus tard + Échap » sont supprimés ; un seul en-tête (titre + croix 28) pour 1.1 à 1.7, modale de lancement 192 de haut. La croix et Échap valent toujours « Plus tard » (`closed(ConnectEnd::Later)`, même hook, même compteur) tant que la modale a été ouverte au lancement et que rien n'a été lancé (`ConnectView::ClosesAsLater()`, ex-`LaunchHeader()`, gardé en alias). API inchangée.

La fenêtre du navigateur est désactivée pendant l'ouverture (`EnableWindow`) et réactivée avant `closed` ; le voile avale les clics. « Terminé » apparaît dès qu'une app est connectée et qu'aucune n'est en cours. La modale n'écrit jamais rien : `connect` est le seul déclencheur, sur clic.

### Panneau de note (2.1 → 2.3)

```cpp
struct NoteContext { std::wstring page /*URL*/, element /*sélecteur*/, text, styles, shotLabel;   // champs V1 (toujours lus)
                     std::wstring site /*« localhost:5173 », vide = déduit de page*/, label /*« Bouton « Payer 48,00 € » »*/, style /*« Taille, couleurs et marges »*/; };
enum class CaptureState { Pending, Ready, Unavailable };
struct NoteRequest { std::wstring draft; NoteContext context; CaptureState capture = Pending; bool expanded = false;
                     std::vector<Project> projects; std::wstring projectId; bool includeShot; };   // les 3 derniers : V1, ignorés
struct NoteSubmission { std::wstring note, projectId /*écho V1*/; bool includeShot /*= capture disponible*/; std::wstring site; };
struct NoteHooks { submit, cancel(draft, keyboard), blurred, announce;  newProject, projectChanged /*V1, jamais appelés*/ };
class NotePopover {
  void Init(HINSTANCE, HWND browser, std::wstring appRoot, NoteHooks);
  bool Show(const NoteRequest&, RECT targetScreenPx, RECT pageScreenPx, bool keyboard, bool reducedMotion);
  void SetShot(CaptureState, const Shot*);   // l'image prise au clic arrive quand elle est prête (Unavailable : pastille 2.3)
  void Saved();                              // le panneau se replie vers le lanceur (150 ms) ; l'hôte montre ensuite la carte
  void Failed(const std::wstring& = …);      // note conservée, « Réessayer » focalisé
  void Close(bool animate = true);
  void SetProjects(...);                     // V1, sans effet
  bool Visible() const; HWND Window() const; HWND Edit() const; HWND PillWindow() const;
};
```
Placement : à côté de l'élément (droite, sinon gauche, sinon dessous, sinon dessus), à 8 DIP du bord de la page ; la pastille est 8 DIP sous le panneau, centrée. `NoteSpec::thumbW/thumbH` = 96×62 (vignette à préparer par l'hôte). Clavier : Entrée = Ajouter (Maj+Entrée = nouvelle ligne, Ctrl+Entrée partout), Tab : champ → Ajouter → pastille, Espace/Entrée sur la pastille la déplie, Échap replie la pastille puis annule (l'hôte garde le brouillon). Validation à l'envoi (jamais de bouton désactivé).

### Lanceur, cartes, historique (3.1 → 4.3)

```cpp
enum class LauncherState { Rest, Working };
enum class CardKind { None, Working, NoAgent, Added, Done, Resolved = Done };
struct CardContent { CardKind kind; std::wstring agent /*« Claude »*/; AgentKind agentKind; std::wstring since /*« depuis 2 min »*/, note, summary, id;
                     std::wstring project, monogram, headline; Shot avatar; /* V1, ignorés */ };
enum class EntryStatus { Pending, Working, Done };
struct HistoryEntry { std::wstring id, site, page, note; EntryStatus status; AgentKind agent; std::wstring agentName, age, followUp, summary; Shot thumb, capture; };
struct HistoryData { std::wstring currentSite; std::vector<HistoryEntry> entries; };   // plus récentes d'abord
struct HistoryHooks {
  std::function<void(const std::wstring& id)> remove, undo, removeFinal, detail;   // suppression immédiate ; « Annuler » 6 s ; oubli définitif à l'expiration
  std::function<void(const std::wstring& markdown)> copy;  std::function<void()> annotate;
  std::function<void(bool keyboard)> closed;  std::function<void(const std::wstring&)> announce;
};
struct LauncherHooks { toggle /*clic, Entrée, UIA*/, open /*« Voir l'annotation »*/, connect, reload, reopen, later /*V1*/, focusOut;
                       std::function<void(bool keyboard)> dismissed; announce; HistoryHooks history; };
class Launcher {
  void Init(HINSTANCE, HWND browser, std::wstring appRoot, LauncherHooks);
  void Place(RECT pageClientPx);                       // bounds du WebView ; ré-ancre et remonte toutes les surfaces
  bool Show(LauncherState, int count, bool reduced);   // Working : anneau + compteur accent ; Rest + count : compteur gris (3.1)
  void Hide(bool animate = true);
  void ShowCard(const CardContent&); void HideCard(bool animate = true);   // même id, autre état : fondu croisé en place
  bool ShowHistory(const HistoryData&, bool keyboard = false);            // l'hôte lit le magasin à ce moment-là seulement
  void UpdateHistory(const HistoryData&);                                 // surveillance du magasin pendant l'ouverture seulement
  void SetHistoryCapture(const std::wstring& id, const Shot&);            // capture pleine taille du détail (hooks.history.detail)
  void HideHistory(bool animate = true);
  void FocusLauncher(); void FocusCard();
  bool Visible() const; bool CardVisible() const; bool HistoryVisible() const;
  HWND LauncherWindow() const, CardWindow() const, HistoryWindow() const, ToastWindow() const;
};
std::wstring EntryMarkdown(const HistoryEntry&);   // « Copier en Markdown » (pur)
```
Toutes ces surfaces sont des fenêtres **enfants** à alpha par pixel au-dessus du WebView : seuls les pixels dessinés prennent la souris (vérifié par `WindowFromPoint`). Ctrl+Maj+H est à router par l'hôte vers `toggle`. Le lanceur se masque en plein écran (hôte : `Hide`). Historique au clavier : Tab (fermer → filtres → liste), ←/→ entre filtres, ↑/↓/Début/Fin/Page↑↓ dans la liste, Entrée = détail, Suppr = supprimer, Maj+F10 ou touche Menu = « … », Ctrl+Z = Annuler, Échap = retour puis fermeture. La molette fait défiler la liste et n'est jamais transmise à la page (`overscroll` contenu, vérifié).

### Migration depuis V1 (pour l'intégrateur)

`annotation-mode.h` compile tel quel (vérifié : `g++ -fsyntax-only src/main.cpp`, 0 avertissement) grâce aux champs V1 conservés. À faire côté intégration : remplacer `Onboarding` par `ConnectModal` (puis retirer `annotation-onboarding.h` d'`annotation-ui.h`), brancher `BannerHooks::connect` + `SetReminder`, `LauncherHooks::{reload, reopen, history}`, remplir `NoteContext::{site,label,style}` et `CardContent::{agentKind, summary, id}`, `CardKind::Resolved` → `Done`, `since` en minuscules (« depuis 2 min »). Aucune signature appelée par `annotation-mode.h` n'a changé : je n'ai modifié aucun fichier d'intégration.

## États

| Surface | États (captures) |
| --- | --- |
| Bandeau | actif (BETA), rappel « Aucun agent connecté · Connecter » (fondu), focus clavier rappel / Quitter, appui 0,96 |
| Modale | lancement (même en-tête que les autres états : titre + croix, sans description), en cours (anneau tournant), connecté (+ note, + Terminé), échec (contour #FF7B6E66 + message), réglage manuel déplié (commande, Copier, JSON, TOML, « Copié »), disque amovible (Reconnecter + pastille d'avertissement), pastille dépliée, focus clavier |
| Note | saisie (champ focalisé, bordure #3E67FF99), texte d'exemple, capture en préparation (anneau), pastille repliée / dépliée, capture impossible, validation (note vide), envoi (« Ajout… »), échec d'écriture (« Réessayer »), focus clavier |
| Lanceur | repos (tiroir), agent au travail (anneau + compteur accent), annotation non lue (compteur gris), historique ouvert (bordure #3E67FF99), survol, appui, focus |
| Cartes | 3.1 ajoutée sans agent, 3.2 l'agent s'en occupe, 3.3 l'agent a terminé, « Annotation ajoutée » (agent connecté, part seule après 4 s), focus clavier |
| Historique | liste groupée par site (page courante d'abord), ligne survolée avec « … », menu « … », filtres En cours / Terminées, filtre vide, détail (en cours / terminé), vide (4.3), défilement interne, toast « Annuler » |

## Mouvement (planches « Spéc » + skills)

| Élément | Valeur |
| --- | --- |
| Modale | voile 0→0,549 en 200 ms ; opacité + échelle 0,96→1 en 200 ms `cubic-bezier(0.23,1,0.32,1)` depuis le centre ; sortie 150 ms vers 0,98 / 0 ; pastille 60 ms après (opacité + 4 DIP) ; `Show(..., delayMs = 80)` après le fondu de 120 ms du bandeau |
| Bouton d'app | appui 0,96 (150 ms ease-out) ; En cours → Connecté / Échec : fondu croisé du contenu et du fond sur ressort critique 0,3 s (98 % à 279 ms), sans changer de place ; les blocs dessous glissent (200 ms `cubic-bezier(0.77,0,0.175,1)`) |
| Réglage manuel | hauteur + opacité 200 ms `cubic-bezier(0.77,0,0.175,1)`, chevron qui tourne avec ; ouvert au clavier = instantané |
| Pastille « local » | 150 ms d'intention (survol ou focus) ; taille sur ressort 0,3 s sans rebond, description 60 ms après (opacité + 4 DIP) ; reste ouverte sous le pointeur ; Échap la replie ; sortie 120 ms |
| Note | 180 ms, origine côté cible + 8 DIP vers la cible ; fenêtre fantôme cliquable-à-travers, la vraie fenêtre (EDIT focalisé dès l'image 0) passe à 255 à l'arrêt ; pastille 60 ms après, se déplie en carte en 200 ms ; « Ajouter » : repli vers le lanceur (bas-droite) en 150 ms |
| Cartes, historique | nés du lanceur (origine = centre du lanceur, bas-droite), 200 ms / 150 ms ; changement d'état de carte : fondu croisé en place + hauteur 200 ms ; détail : glisse de 16 DIP depuis la droite en fondu croisé |
| Interruption | toutes les rampes repartent de la valeur présentée (mesuré : 0,8430 → 0,8430 au moment de l'interruption) ; le fondu croisé repart de l'alpha affiché |
| Mouvement réduit | fondus seuls (modale 150 ms / 120 ms), ni échelle ni glissement, mise en page instantanée, anneau fixe (le compteur reste) |
| Horloge | `frame-clock` seulement pendant une animation ; exception assumée : l'anneau « en cours » tourne tant qu'un agent travaille (lanceur, bouton En cours) ; minuteries ponctuelles seulement (intention 150 ms, « Copié » 1,6 s, toast 6 s, carte 4 s) |

Indice statique pour chaque état animé : couleur (accent, danger), icône (anneau, coche, logo, avertissement) ou texte (« Cliquez sur Installer… », « Copié », « En attente »).

## Accessibilité

- UIA (`accessibility.h`) sur chaque surface : bandeau (groupe « Mode Annotation », statut, « Connecter un agent », « Quitter le mode Annotation » + Échap) ; modale (groupe titré, croix « Fermer, connecter un agent plus tard » à l'ouverture (automationId `AnnotationConnectLater`) puis « Fermer » (`AnnotationConnectClose`), + Échap, boutons d'app avec statut En cours / Connecté / Échec et message en aide, « Terminé » + Entrée, « Autre app ou réglage manuel » expand/collapse, « Copier la commande », « Copier au format JSON / TOML », pastille expand/collapse) ; note (groupe, champ nommé « Que faut-il changer ? » + raccourcis + erreur en aide, « Site » en texte, « Ajouter » + Entrée, pastille expand/collapse, lignes Page / Élément / Style, capture) ; lanceur (« Annotations » / « Annotations · 1 en cours », Ctrl+Maj+H, expand) ; cartes (groupe, texte, boutons) ; historique (onglets « Toutes / En cours (n) / Terminées (n) », liste d'éléments avec statut, position, hors-écran, détail, « Supprimer » annulable) ; toast (« Annuler », Ctrl+Z).
- Ordre Tab visuel partout, Échap ferme (d'abord l'élément déplié), focus visible (anneau 1 DIP blanc, 50 % clavier / 10 % pointeur) sur chaque contrôle dessiné, la modale piège le focus (navigateur désactivé).
- Annonces : « Mode Annotation activé », « Connecter un agent », « Cliquez sur Installer dans Claude », « Claude Desktop connecté », « Échec : … », « Copié », « Ce que l'agent verra : affiché », « Écrivez ce qu'il faut changer. », « Annotation ajoutée. Aucun agent ne la lit encore. », « Claude s'en occupe, depuis 2 min », « Historique, 4 annotations », « Annotation supprimée. Ctrl+Z pour annuler. », « Suppression annulée ».

## Écarts assumés avec Paper

1. **Ombres portées non dessinées** (modale, cartes, historique, lanceur) : règle du dépôt « zero drop shadow » (`src/native-ui.h`).
2. **« BETA »** : #5B84FF sur sa pastille accent 16 % mesure 4,14:1 (12 px) ; éclairci en #6A8FFF (4,67:1). « Mode Annotation » garde #5B84FF.
3. **Lignes terminées de l'historique** : opacité 70 % sur la vignette et la note seulement ; statut et âge (12 px #FFFFFF99) restent à 100 % (6,2:1 au lieu de 4,1:1).
4. **2.1** montre « Ce que l'agent verra » dans le coin du panneau ; j'ai suivi 2.2/2.3 et la planche « Spéc · Annotations » (site globe + origine dans le panneau, pastille séparée dessous).
5. Âge aligné à droite sur la même colonne dans toutes les lignes (4.1 décale de 8 DIP la ligne survolée).
6. Graisse 600 rendue Medium (seuls Regular/Medium d'Inter Display sont embarqués) ; pas de flou 4 px dans les fondus croisés (cibles D2D sans effets) — fondu d'opacité seul.
7. Icône tiroir à 18 DIP (1.1, 4.2, 4.3) ; 3.x l'affiche à 24.
8. Hauteurs Paper arrondies : ±1 DIP sur 1.2 / 1.4 / 1.5 / 1.6 / 4.1 (texte 24 px arrondi par Paper).
9. Champ de note : EDIT natif, hauteur de ligne GDI (~17 au lieu de 20) une fois posé (le rendu d'entrée est en DirectWrite 14/20).

## Preuves (harnais)

```
python tests/annotations-ui/run.py               # build + tests + captures + build/annotations-ui-qa/v2/report.json
python tests/annotations-ui/run.py --no-window   # sans fenêtres réelles
python tests/annotations-ui/tables.py            # tables ci-dessous depuis report.json
```
Le harnais prend le premier plan (hôte topmost) pour les vrais clics : ne pas taper au clavier pendant ~15 s. Résultat (Windows 11 26200, 125 %) : **440 vérifications, 0 échec** (révision 1.1), sur 3 passes complètes consécutives (V2-B initiale : 426/426 sur 4 passes) (et 289/289 sans fenêtres). Aucun échec intermittent observé sur ces passes ; un `WM_TIMER` « étranger » (fenêtre hors annotation) est parfois imprimé pendant le repos, il ne compte pas.

### Échantillons de mouvement (modèle pur)

Modale : entrée 200.0 ms, sortie 150.0 ms, mouvement réduit 150.0 ms ; fondu croisé d'état à 98 % en 279.0 ms.

| Surface | Instant | t (ms) | opacité | échelle | dx (DIP) | dy (DIP) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| modal | enter t=0 | 0.0 | 0.0000 | 0.9600 | 0.000 | 0.000 |
| modal | enter t=1/4 | 50.0 | 0.7754 | 0.9910 | 0.000 | 0.000 |
| modal | enter t=1/2 | 100.0 | 0.9660 | 0.9986 | 0.000 | 0.000 |
| modal | enter t=3/4 | 150.0 | 0.9974 | 0.9999 | 0.000 | 0.000 |
| modal | enter t=end | 200.0 | 1.0000 | 1.0000 | 0.000 | 0.000 |
| modal | exit t=1/2 | 75.0 | 0.0340 | 0.9807 | 0.000 | 0.000 |
| modal | exit t=end | 150.0 | 0.0000 | 0.9800 | 0.000 | 0.000 |
| modal | interrupt before | 60.0 | 0.8430 | 0.9937 | 0.000 | 0.000 |
| modal | interrupt after | 60.0 | 0.8430 | 0.9937 | 0.000 | 0.000 |
| modal | interrupt +40ms | 100.0 | 0.1682 | 0.9827 | 0.000 | 0.000 |
| modal | reduced t=1/2 | 75.0 | 0.9660 | 1.0000 | 0.000 | 0.000 |
| note | enter t=1/2 | 90.0 | 0.9660 | 0.9986 | -0.272 | 0.000 |
| card | enter t=1/2 | 100.0 | 0.9660 | 0.9986 | 0.000 | 0.000 |

Modale dans une vraie fenêtre : 282 ms, 22 images (horloge compositeur).

Carte dans une vraie fenêtre : 235 ms, 22 images (horloge compositeur).

Repos 1500 ms (card) : images 0, requêtes d'horloge 0, réveils 0, ticks 0, messages postés 0, WM_TIMER des surfaces 0 (autres fenêtres : 0).

Repos 1500 ms (rest) : images 0, requêtes d'horloge 0, réveils 0, ticks 0, messages postés 0, WM_TIMER des surfaces 0 (autres fenêtres : 1).

### Géométrie Paper (modèle de boîtes CSS) vs layout natif

| Surface | largeur | Paper | natif |
| --- | ---: | ---: | ---: |
| 1.1 modal (launch) | 440 | 192.00 | 193.00 |
| 1.2 modal (en cours) | 440 | 192.39 | 193.00 |
| 1.3 modal (connecté + Terminé) | 440 | 261.00 | 261.00 |
| 1.4 modal (échec) | 440 | 216.00 | 217.00 |
| 1.5 modal (réglage manuel) | 440 | 305.00 | 306.00 |
| 1.5 footer unfolded | 438 | 158.00 | 158.00 |
| 1.6 modal (disque amovible) | 440 | 192.00 | 193.00 |
| 1.1 pill « Reste sur cet ordinateur » width | 0 | 161.00 | 159.25 |
| 1.6 pill (disque amovible) width | 0 | 389.00 | 388.00 |
| 1.7 pill unfolded height | 340 | 100.00 | 100.00 |
| 1.1-1.7 header | 438 | 62.00 | 62.00 |
| 2.1 note panel (2 lines) | 350 | 102.00 | 102.00 |
| 2.1 note panel min-height | 350 | 84.00 | 84.00 |
| 2.2 pill unfolded | 350 | 111.19 | 112.00 |
| 2.3 « Capture impossible » pill width | 0 | 320.00 | 318.00 |
| 3.1 card | 320 | 118.59 | 119.00 |
| 3.2 card | 320 | 138.00 | 138.00 |
| 3.3 card | 320 | 138.00 | 138.00 |
| 4.1 history panel | 360 | 362.00 | 363.00 |
| 4.2 detail | 360 | 378.59 | 379.00 |
| 4.3 empty | 360 | 203.59 | 204.00 |

### Couleurs Paper échantillonnées dans les rendus (2x)

| Point | Paper (composé) | Rendu |
| --- | --- | --- |
| 1.1 modal surface #171717 | `#171717` | `#171717` |
| 1.1 « Connecter avec Claude » raised #292929 | `#292929` | `#292929` |
| 1.1 modal border #FFFFFF14 | `#292929` | `#292929` |
| Claude logo #D97757 | `#D97757` | `#D97757` |
| 1.2 en cours #FFFFFF0A | `#202020` | `#202020` |
| 1.3 connecté: no fill (contour only) | `#171717` | `#171717` |
| 1.3 « Terminé » accent | `#3E67FF` | `#3E67FF` |
| 1.4 échec border #FF7B6E66 | `#7F4A45` | `#7F4A45` |
| 1.5 command box raised | `#292929` | `#292929` |
| veil #0000008C over the page | `#6E6E6E` | `#6E6E6E` |
| 1.8 reminder chip #FFFFFF14 over the band | `(composited)` | `#2B2F46` |
| BETA chip accent 16 % over the band | `(composited)` | `#1F2856` |
| 2.1 note surface #171717 | `#171717` | `#171717` |
| 2.1 focused border #3E67FF99 | `#2E47A2` | `#2E47A2` |
| 2.1 « Ajouter » accent | `#3E67FF` | `#3E67FF` |
| 3.1 card surface #171717 | `#171717` | `#171717` |
| 3.1 « Connecter un agent » raised | `#292929` | `#292929` |
| 3.1 check disc accent | `#3E67FF` | `#3E67FF` |
| 3.1 launcher badge raised #292929 | `#292929` | `#292929` |
| 3.2 launcher badge accent | `#3E67FF` | `#3E67FF` |
| launcher surface #171717 | `#171717` | `#171717` |
| 4.1 chosen segment #3E3E3E | `#3E3E3E` | `#3E3E3E` |
| 4.1 filters raised #292929 | `#292929` | `#292929` |
| 4.1 hovered row #FFFFFF0A | `#202020` | `#202020` |
| 4.2 « Supprimer » danger 10 % | `#2F2120` | `#2F2120` |
| 4.3 « Annoter cette page » accent | `#3E67FF` | `#3E67FF` |

### Contrastes mesurés (pixels rendus à 2x, puis paire de jetons)

| Texte | mesuré | jetons | encre | fond |
| --- | ---: | ---: | --- | --- |
| modal title 18/24 white | 17.93:1 | 17.93:1 | `#FFFFFF` | `#171717` |
| close cross 16 at 70 % (non-text, 3:1) | 9.14:1 | 9.22:1 | `#B9B9B9` | `#171717` |
| « Connecter avec Claude » white on #292929 | 14.55:1 | 14.55:1 | `#FFFFFF` | `#292929` |
| « Autre app ou réglage manuel » 13/18 #FFFFFFB3 | 9.24:1 | 9.22:1 | `#BABABA` | `#171717` |
| pill « Reste sur cet ordinateur » 12/16 #FFFFFF99 | 7.02:1 | 7.04:1 | `#A2A2A2` | `#171717` |
| « Cliquez sur Installer dans Claude » #FFFFFFCC | 10.76:1 | 10.81:1 | `#D2D2D2` | `#202020` |
| « Redémarrez Codex pour l'activer » 12/16 #FFFFFF99 | 7.02:1 | 7.04:1 | `#A2A2A2` | `#171717` |
| « Terminé » white on accent | 4.58:1 | 4.58:1 | `#FFFFFF` | `#3E67FF` |
| échec message 12/16 #FF7B6E | 7.09:1 | 7.09:1 | `#FF7B6E` | `#171717` |
| command JetBrains Mono 12/16 #FFFFFFCC | 9.81:1 | 9.83:1 | `#D4D4D4` | `#292929` |
| « Copier au format : » 12/16 #FFFFFF99 | 7.02:1 | 7.04:1 | `#A2A2A2` | `#171717` |
| manual text 13/19 #FFFFFFB3 | 9.24:1 | 9.22:1 | `#BABABA` | `#171717` |
| removable pill 12/16 #FFFFFFCC | 11.74:1 | 11.69:1 | `#D1D1D1` | `#171717` |
| local pill description 12/18 #FFFFFFB3 | 9.24:1 | 9.22:1 | `#BABABA` | `#171717` |
| band « Aucun agent connecté » 12/16 #FFFFFFCC | 9.06:1 | 9.00:1 | `#D5D6DC` | `#2B2F46` |
| band « Connecter » 12/16 medium white | 13.14:1 | 13.14:1 | `#FFFFFF` | `#2B2F46` |
| band « BETA » 12 medium #6A8FFF (Paper #5B84FF: 4.14:1) | 4.67:1 | 4.67:1 | `#6A8FFF` | `#1F2856` |
| note text 14/20 #FFFFFFE6 | 14.63:1 | 14.67:1 | `#E8E8E8` | `#171717` |
| site 12/16 #FFFFFF99 | 7.02:1 | 7.04:1 | `#A2A2A2` | `#171717` |
| « Ajouter » 13/16 white on accent | 4.58:1 | 4.58:1 | `#FFFFFF` | `#3E67FF` |
| pill « Ce que l'agent verra » 12/16 #FFFFFF99 | 7.02:1 | 7.04:1 | `#A2A2A2` | `#171717` |
| placeholder « Que faut-il changer ? » #FFFFFF99 | 7.02:1 | 7.04:1 | `#A2A2A2` | `#171717` |
| pill row label 12/16 #FFFFFF99 | 7.02:1 | 7.04:1 | `#A2A2A2` | `#171717` |
| pill row value 12/16 #FFFFFFE6 | 14.63:1 | 14.67:1 | `#E8E8E8` | `#171717` |
| pill header 12/16 medium #FFFFFFE6 | 14.63:1 | 14.67:1 | `#E8E8E8` | `#171717` |
| « Capture impossible ici » 12/16 #FFFFFFCC | 11.74:1 | 11.69:1 | `#D1D1D1` | `#171717` |
| write error 12/16 #FF7B6E | 7.09:1 | 7.09:1 | `#FF7B6E` | `#171717` |
| card title 14/20 600 white | 17.93:1 | 17.93:1 | `#FFFFFF` | `#171717` |
| card body 13/19 #FFFFFFB3 | 9.24:1 | 9.22:1 | `#BABABA` | `#171717` |
| card action 13/16 white on #292929 | 14.55:1 | 14.55:1 | `#FFFFFF` | `#292929` |
| card « · depuis 2 min » 14/20 #FFFFFF99 | 17.93:1 | 7.04:1 | `#FFFFFF` | `#171717` |
| « Historique » 16/20 600 white | 17.93:1 | 17.93:1 | `#FFFFFF` | `#171717` |
| segment 12/16 medium #FFFFFFB3 on #292929 | 7.91:1 | 7.93:1 | `#BFBFBF` | `#292929` |
| segment count 11/14 #FFFFFF80 on #292929 | 4.80:1 | 4.82:1 | `#949494` | `#292929` |
| section site 12/16 medium #FFFFFFB3 | 9.24:1 | 9.22:1 | `#BABABA` | `#171717` |
| « · cette page » 11/14 #FFFFFF80 | 5.26:1 | 5.29:1 | `#8B8B8B` | `#171717` |
| row status 12/16 #FFFFFF99 | 6.69:1 | 6.67:1 | `#A6A6A6` | `#202020` |
| done row status 12/16 #FFFFFF99 (kept at 100 %) | 9.14:1 | 7.04:1 | `#B9B9B9` | `#171717` |
| done row note 13/18 white at 70 % | 9.14:1 | 9.22:1 | `#B9B9B9` | `#171717` |
| « Supprimer » 12/16 #FF7B6E on danger 10 % | 6.11:1 | 6.11:1 | `#FF7B6E` | `#2F2120` |
| follow-up 12/17 #FFFFFF99 on #292929 | 6.19:1 | 6.22:1 | `#A9A9A9` | `#292929` |
| detail page 13/18 #FFFFFFB3 | 9.24:1 | 9.22:1 | `#BABABA` | `#171717` |
| « Copier en Markdown » 12/16 white on #292929 | 14.55:1 | 14.55:1 | `#FFFFFF` | `#292929` |
| empty « Aucune annotation pour l'instant... » 14/20 #FFFFFF80 | 5.26:1 | 5.29:1 | `#8B8B8B` | `#171717` |
| toast « Annotation supprimée » 13/16 #FFFFFFE6 | 14.63:1 | 14.67:1 | `#E8E8E8` | `#171717` |

Police mono résolue : JetBrains Mono.

### Captures (`build/annotations-ui-qa/v2/`, 79)

`1.1-connect-launch.png`, `1.2-connect-working.png`, `1.3-connect-connected.png`, `1.4-connect-failed.png`, `1.5-connect-manual.png`, `1.5b-connect-manual-copied.png`, `1.6-connect-removable.png`, `1.7-connect-local-unfolded.png`, `1.1b-connect-focus-keyboard.png`, `1.1-scene-launch.png`, `1.8-scene-band-reminder.png`, `1.1c-band-beta.png`, `2.1-note-panel.png`, `2.1b-note-placeholder.png`, `2.2-note-what-the-agent-sees.png`, `2.2b-note-capture-pending.png`, `2.3-note-capture-unavailable.png`, `2.4-note-empty-validation.png`, `2.5-note-saving.png`, `2.6-note-write-failed.png`, `2.7-note-focus-add-keyboard.png`, `2.1-scene-write-note.png`, `3.1-card-added-no-agent.png`, `3.2-card-agent-working.png`, `3.3-card-agent-done.png`, `3.4-card-added-agent-connected.png`, `3.3b-card-done-focus-keyboard.png`, `3.2b-card-codex-working.png`, `3.0-launcher-rest.png`, `3.0b-launcher-working.png`, `3.0c-launcher-history-open.png`, `3.0d-launcher-focus-keyboard.png`, `3.0e-launcher-hover.png`, `4.1-history-list.png`, `4.1b-history-row-menu.png`, `4.1c-history-filter-open.png`, `4.1d-history-filter-done-keyboard.png`, `4.2-history-detail.png`, `4.2b-history-detail-done.png`, `4.3-history-empty.png`, `4.3b-history-filter-empty.png`, `4.4-history-scrolled.png`, `4.5-history-toast-undo.png`, `4.1-scene-history.png`, `3.2-scene-card-working.png`, `m1-modal-enter-0-50-100-150-200ms.png`, `m2-modal-exit-0-50-100-150ms.png`, `m3-modal-interrupt-at-60ms.png`, `m4-modal-reduced-fade.png`, `m5-button-crossfade-working-to-connected.png`, `m6-local-pill-unfold.png`, `m7-card-from-launcher.png`, `m8-history-back-to-launcher.png`, `m9-note-pill-unfold.png`, `w1-window-band-reminder.png`, `w2-window-modal-entering.png`, `w3-window-modal-launch.png`, `w4-window-modal-crossfade-working-to-connected.png`, `w5-window-modal-connected.png`, `w6-window-modal-failed.png`, `w7-window-modal-manual.png`, `w8-window-modal-local-unfolded.png`, `w3b-window-modal-launch-cross.png`, `w9-window-note-entering.png`, `w10-window-note-pill.png`, `w11-window-note-pill-unfolded.png`, `w12-window-note-capture-unavailable.png`, `w13-window-launcher-rest.png`, `w14-window-card-entering.png`, `w15-window-card-no-agent.png`, `w16-window-card-crossfade-working-to-done.png`, `w17-window-card-done.png`, `w18-window-history-entering.png`, `w19-window-history.png`, `w20-window-history-detail-sliding.png`, `w21-window-history-detail.png`, `w22-window-toast-undo.png`, `w23-window-history-scrolled.png`, `w24-window-history-empty.png`


## Historique

V1 (bandeau, popover avec sélecteur de projet, lanceur + carte, onboarding en 3 étapes) : voir ce fichier au commit `120999a`.

## V3-A — corrections du premier test réel (14 novembre 2026)

Retours : `plan-annotations-mcp-local/design/annotations-v2-retours.md` (points 1, 2, 3, 5, 6, glissé du bandeau, 11). Références Paper relues en lecture seule : section « 5 · Retours du test » (5.2 chronologie d'entrée, 5.5 suivi par l'agent, 5.6 redémarrer, 5.7 aperçu vertical, 5.8 hors mode). Ce qui suit remplace les passages V2 correspondants.

| Sujet | Comportement V3-A | Fichiers |
| --- | --- | --- |
| Lanceur (§6, 5.8) | N'existe qu'en mode Annotation : apparaît 120 ms après le bandeau, 0,9 → 1 + opacité en 200 ms (`LauncherTiming`, retard passé à `Show(…, delayMs)`) ; repart dans son coin en 150 ms à la sortie. Hors mode, `SetDormant(true)` détruit ses fenêtres (bouton, carte, historique, toast) dès que plus rien n'est à l'écran. Ctrl+Maj+H ouvre l'historique seul, ancré à 16 DIP du bas (`HistoryPanel::SetLowAnchor`), le toast à gauche du panneau. | `annotation-launcher.h`, `annotation-history.h`, `annotation-mode.h` |
| Bandeau (glissé, 5.2) | Glisse de −32 DIP à 0 + fondu, 220 ms `cubic-bezier(0.32, 0.72, 0, 1)` (`Curves::band`) ; sortie 160 ms par le même chemin ; interruptible depuis la valeur présentée (une seule rampe d'opacité, le décalage en dérive) ; mouvement réduit : fondu 150 ms. Fond animé futur : `Banner::SetBackdrop(paint, animates)` (dessiné sous le contenu, découpé au bandeau, reçoit le temps). Dégradé du viewport, vague tramée, curseur dédié et inspecteur d'espacements : non faits (en design). | `annotation-banner.h`, `annotation-motion.h` |
| Capture sans flash (§2) | Le cadre de la page reste à l'écran pendant la prise. Une copie native des bords et de l'étiquette (`SelectionFrame`, fenêtre enfant en couches, cliquable-à-travers) est posée aux mêmes pixels et attend le compositeur (`DwmFlush`) ; puis la page masque SES bords et son étiquette (« handoff ») et répond « hidden » dans la même tâche ; la prise suit ; « show » remet ceux de la page, qui répond « shown » deux images plus tard ; alors seulement la copie native part. Le voile de 8 % reste dans la page (deux voiles translucides superposés fonceraient) et est retiré de l'image par calcul (`RemoveFill` : couverture exacte des bords, défilement arrondi au pixel comme le moteur). Police de l'étiquette native : celle que la page résout (`Font::system`). | `annotation-frame.h`, `annotation-script.h`, `annotation-capture.h`, `annotation-mode.h` |
| Prise de vue | `Page.captureScreenshot` **sans** clip (la vue entière, telle qu'à l'écran) puis recadrage par l'hôte (`ProcessViewport`, décodage arrêté à la dernière ligne utile) : une prise avec clip faisait re-rendre la vue à l'écran et le texte de la page disparaissait ~4 images à chaque clic (mesuré, §3). Le panneau de note est construit juste après la demande au moteur (construit avant, ses 30–55 ms retardaient le « hidden » de la page). | `annotation-mode.h`, `annotation-capture.h` |
| Suivi par l'agent (§5, 5.5) | Ligne : « Lue par Claude » (œil 12 à 70 %) quand un agent a lu l'annotation (le connecteur note les ids lus) sans la prendre ; « Claude n'a pas pu la traiter » (warning-circle + texte ambre `#E6AC3D`, ligne à pleine opacité, comptée dans « En cours ») après un `release` avec raison. Détail : encadré « Suivi » avec les étapes horodatées (Ajoutée, Lue par, Prise en charge, Rendue / n'a pas pu la traiter + raison citée, Terminée + résumé, Question, Écartée, Rouverte) et, pour une annotation non traitée, l'aide « Pour qu'un agent modifie le site, ouvrez Claude Code ou Codex dans le dossier du projet, puis rouvrez l'annotation. ». Cartes du lanceur : « Lue par Claude » et « Claude n'a pas pu la traiter » (+ « Voir l'annotation »). Écart : le bouton « Rouvrir » du détail de la planche 5.5 n'est pas ajouté (l'annotation est déjà ouverte ; « Copier en Markdown » / « Supprimer » restent) : à trancher avec le design final. | `annotation-history.h`, `annotation-launcher.h`, `annotation-mode.h` |
| Redémarrer (§1, 5.6) | « Redémarrez Claude pour l'activer » sous le bouton connecté tant que l'app n'a pas lu le magasin depuis la connexion (même composant que Codex) ; les deux à redémarrer : une seule ligne « Redémarrez Claude et Codex pour les activer ». | `annotation-connect.h`, `annotation-mode.h` |
| « Ce que l'agent verra » (§11, 5.7) | Image toujours en `contain` (jamais rognée, fond de page neutre, jamais agrandie au-delà de ses pixels). Image plus large que 2,2:1 → disposition verticale : image sur toute la largeur (326 × 64), puis Page / Élément / Style. Même règle `contain` pour la capture du détail de l'historique. | `annotation-note.h`, `annotation-kit.h`, `annotation-history.h`, `annotation-capture.h` |

### Flashs traqués (§3) : causes et corrections

| # | Cause (où, quand) | Correction |
| --- | --- | --- |
| 1 | Au clic, la page masquait tout son cadre le temps de la prise (V2, voulu) : le cadre disparaissait ~50–70 ms. | Copie native + « handoff » ordonné (ci-dessus). |
| 2 | `Page.captureScreenshot` avec clip : le moteur re-rend la vue pour la zone ; le texte de la page disparaissait ~4 images (30–110 ms après le clic), à chaque clic, V2 compris. | Prise de la vue entière + recadrage par l'hôte. |
| 3 | Surfaces enfants en couches (bandeau, lanceur, cartes, historique, toast, copie du cadre) : `SetWindowPos` puis `UpdateLayeredWindow` = deux mises à jour du compositeur ; une composition entre les deux montrait les anciens pixels à la nouvelle place / taille (à chaque image d'une animation de hauteur ou d'un déplacement). | Un seul `UpdateLayeredWindow` avec position + taille + pixels (pour un enfant, `pptDst` est en coordonnées client du parent : vérifié par un programme de test). |
| 4 | Fin de l'entrée du panneau de note : le fantôme animé était masqué avant que la vraie fenêtre soit peinte et opaque. | Vraie fenêtre peinte, puis opaque, `DwmFlush`, puis fantôme masqué (recouvrement, jamais de trou). |
| 5 | Début de la sortie du panneau : la vraie fenêtre était masquée avant que le fantôme soit présenté. | Fantôme présenté, `DwmFlush`, puis vraie fenêtre masquée. |
| 6 | Panneau qui grandit (nouvelle ligne tapée) : la fenêtre grandissait avant d'être repeinte (bande non initialisée sous le champ). | Peinture immédiate (`UpdateWindow`) après l'agrandissement. |
| 7 | Cadre de la page qui revient (pointeur qui revient sur la page, nouvel élément après une sortie) : toutes les transitions étaient coupées, le fondu de sortie en cours sautait à 0 puis repartait de 0. | Seule la position saute ; l'opacité continue depuis la valeur affichée (`moveOnly`). |
| 8 | Clic pendant le fondu d'entrée du cadre : voile presque transparent dans la prise (et l'hôte retirait 8 % absents). | Au clic, le cadre passe à son état final (transitions coupées en ligne) et la page envoie l'opacité réelle du voile. |
| 9 | Lanceur masqué d'un coup quand il n'était plus voulu. | Sortie animée (dans son coin, 150 ms) puis destruction des fenêtres. |

Vérifiés sans changement : chaque surface en couches est présentée (pixels) avant son `ShowWindow` (pas d'affichage avant le premier rendu) ; le voile de la modale est effacé en noir avant que son alpha monte ; une fenêtre recréée après destruction l'est à son premier rendu (opacité 0). Non traité (inhérent) : le décalage de 32 DIP de la page à l'entrée / la sortie du mode (redimensionnement du WebView) ; le bandeau qui glisse l'accompagne.

## V3-B — entrée animée, bandeau vivant, curseur, espacements au survol (14 novembre 2026)

Planches Paper 5.1 à 5.5 (relues en lecture seule ; valeurs par `get_computed_styles` / `get_jsx`). Formules des trames : `gen_dither.py` de la tâche de design (Bayer 4×4, cellules 2 DIP bandeau / 3 DIP page, 1 px vide par cellule).

| Élément | Où | Comportement |
| --- | --- | --- |
| Modèle pur | `src/annotation-dither.h` | Bayer, champ de la vague, zones sans texte (`Zones`, `Mask` smoothstep 120 DIP), amplitude (0 avant 220 ms, montée 400 ms), `VeilTimeline` (140 / 80 / 500 ms, interruption 160 ms depuis la valeur affichée, réduit 50 % / 150 + 150 ms), rasters BGRA, taille du curseur (32 / 48 / 64). |
| Bandeau vivant | `src/annotation-banner.h` | Trame de base partout (0,18·y/H, opacité 0,85), vague bande + enveloppe ; zones calculées sur `LayoutBanner` (fin de « Mode Annotation » / BETA → début de « Quitter », ou du rappel au fil de son fondu). 30 images/s au plus : une image de vague ne réécrit que l'intervalle entre les libellés, en pixels, sans Direct2D (`RasterBandSpan` + `Layer::PresentRegion`, `UpdateLayeredWindowIndirect` + `prcDirty`), égal au rendu Direct2D à 1 niveau près ; entre deux images, un minuteur rend la main à l'horloge d'affichage juste avant l'échéance (≈ 26 réveils d'horloge/s au lieu de 120). Horloge arrêtée (et temps de vague figé) fenêtre réduite, masquée ou application en arrière-plan (`AnnotationWindowState` sur `WM_ACTIVATEAPP`, `WM_SHOWWINDOW`, `WM_SIZE`), et à la sortie. Réduit : trame fixe. |
| Voile d'entrée | `src/annotation-veil.h` (`EntryVeil`) | Fenêtre enfant en couches au-dessus de la zone page, `WS_EX_TRANSPARENT` + `WS_EX_NOACTIVATE`, créée à l'entrée, **détruite** à la fin. #3E67FF : lavis 0,2 → 0 sur 160 DIP depuis le bandeau, liseré intérieur (ombre 48 px, σ 24, alpha 0,22) à gauche, à droite, en bas, trame qui ondule (6 rad/s). Pixels écrits directement (`Layer::PresentPixels`) : ≈ 1 ms de calcul par image. Pas de voile à la sortie. |
| Curseur | `src/annotation-script.h` | Feuille construite adoptée par le document (et les racines d'ombre ouvertes traversées) : `*, *::before, *::after { cursor: image-set(url(svg) <échelle écran>x) hx hy, default !important }` ; boucliers des iframes (deux origines) avec le même curseur. Image 32 / 48 / 64 px selon DPI × `CursorBaseSize` ; résolution = échelle de l'écran fournie par l'hôte (jamais `devicePixelRatio`, qui inclut le zoom) ; point chaud en coordonnées CSS de l'image. Retirée à la sortie (aucun style restant). UI native : flèche, champ de note : I-beam (inchangés). |
| Inspecteur | `src/annotation-script.h`, `src/annotation-frame.h` | Dans la racine fermée du cadre : bandes de padding (135°, 1 px / 5 px, accent 40 % sur 8 %) sous la bordure (coins intérieurs arrondis), gaps (45°, rose 45 % sur 12 %) entre les rectangles réels des enfants (lignes par recouvrement vertical, wrap compris ; flex = espace réel, grid = largeur du gap avant la piste suivante), étiquettes JetBrains Mono 10/14 (pastille accent / #D63E92), une par valeur distincte **et par axe** (la planche 5.3 montre « 20 » en haut et à gauche), aucune sous 4 px. Masqué tout de suite au changement de cible, montré 120 ms après le repos du pointeur (le glissé du cadre) : rien pendant un mouvement rapide. À la sélection, sa géométrie part avec celle du cadre (`mark.sp`), la copie native (`SelectionFrame::PaintHatch`, même phase que le dégradé CSS) la dessine, le « handoff » masque celle de la page : jamais dans la capture. |
| Style | `src/annotation-host.h` (`StyleSummary`) | La ligne « Style » de la pastille « Ce que l'agent verra » : « padding 18 20 · gap 14 (flex) » quand l'élément en a, sinon « Taille, couleurs et marges ». |
| Rouvrir | `src/annotation-history.h` | Détail d'une annotation rendue avec une raison : « Rouvrir » (relief, ajusté au texte) · « Copier en Markdown » … « Supprimer » ; ordre Tab Retour, Fermer, Rouvrir, Copier, Supprimer ; nœud UIA « Rouvrir ». L'hôte : `Store::Reopen` (état ouvert, suivi « Rouverte » horodaté), historique relu, compteur du lanceur recalculé. |

Écarts assumés : la vague de `gen_dither.py` faisait sauter son enveloppe de 0,85 W à 0,15 W à chaque boucle ; ici l'enveloppe parcourt une largeur entière par passe, enroulée (boucle sans couture, testée). La vague « démarre » à 220 ms avec une montée d'amplitude de 400 ms (pas de pop). Courbes du voile : montée ease-out, descente ease-in-out (non précisées dans Paper).
