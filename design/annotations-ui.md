# Mode Annotation — UI native (V1)

Composants natifs du mode Annotation : bandeau, état « actif » du bouton barre « Annoter », popover de note, lanceur + carte, onboarding « Connecter un agent ». Contrat : `plan-annotations-mcp-local/design/annotations-v1-brief.md` (références Paper, mouvement, budget). Maquettes : Paper `01M6Q7YT14ZKMGET54ASPNGQEC`, page `p-H-0` (lues avec `get_jsx` / `get_computed_styles`, rien modifié).

| Fichier | Rôle |
| --- | --- |
| `src/annotation-ui.h` | Include unique pour l'hôte |
| `src/annotation-motion.h` | Modèle de mouvement pur (ressorts, pose, fondu croisé, anneau) — testé sans fenêtre |
| `src/annotation-kit.h` | `Ink` (D2D/DirectWrite/WIC partagé, texte Inter Display + mono, glyphes Phosphor exacts), `Layer` (fenêtre alpha par pixel), `Animator` (une horloge `frame-clock.h` pour toutes les surfaces), rendu hors écran pour la QA |
| `src/annotation-banner.h` | `Banner` + `PaintToolbarButton` + `ToolbarNode` |
| `src/annotation-note.h` | `NotePopover` (EDIT natif multi-ligne, liste Projet, repli contexte, info-bulle, états) |
| `src/annotation-launcher.h` | `Launcher` (lanceur 40 + carte) |
| `src/annotation-onboarding.h` | `Onboarding` (voile + modale 3 étapes) + textes de config purs |
| `tests/annotations-ui/` | Harnais natif (`annotations_ui_test.cpp`, `run.py`, manifeste Windows 8+) |

Aucune E/S disque (hors lecture des polices embarquées), aucune dépendance à `annotations.h` : des structures simples en entrée, des callbacks en sortie.

## Coût zéro et création paresseuse

- `Init(...)` ne fait que stocker instance, fenêtre parente/propriétaire, racine et hooks : aucune fenêtre, aucun thread, aucune ressource D2D (vérifié : `Ink::Exists()`, `Animator::Exists()`, `Layer::Created()` restent faux/0 après les quatre `Init`).
- Le premier `Show` d'une surface crée `Ink` (une fois par processus) et ses fenêtres. Les fenêtres restent ensuite cachées entre deux usages.
- `Animator` : une fenêtre message-only + un `FrameClock::Clock`, créés à la **première animation**. Le thread de l'horloge est bloqué (`INFINITE`) dès que plus rien ne bouge. Pas de `SetTimer` récurrent (seul repli : 16 ms si l'horloge compositeur ne démarre pas, tué dès la fin du mouvement).
- Minuteries ponctuelles (une seule fois, tuées à l'expiration ou à la fermeture) : carte « Annotation ajoutée » (4 s, en pause au survol/focus) et lien « Ça ne marche pas ? » (2 min, onboarding ouvert seulement).
- Exception assumée : l'anneau « en cours » tourne (frames vsync) tant qu'un agent travaille et que le lanceur ou la carte est visible ; fixe en mouvement réduit.

## API d'intégration (pour T3, `src/main.cpp`)

```cpp
#include "annotation-ui.h"
using namespace NativeChrome::Annotations;
```

### Bouton barre « Annoter » (dessiné par la Chrome existante)

```cpp
struct ToolbarVisual { bool active; float hover, press; bool focus, keyboard, reduced; };
void PaintToolbarButton(ID2D1RenderTarget*, ID2D1Factory*, ID2D1SolidColorBrush*, D2D1_RECT_F box24, const ToolbarVisual&);
Accessibility::Node ToolbarNode(long long key, long long parent, RECT screenBounds, bool active); // « Annoter », Ctrl+Maj+A, toggle
```
Appel depuis la passe de peinture : `PaintToolbarButton(chrome.RenderTarget(), chrome.Factory(), chrome.SolidBrush(), box, v)`. Actif : plaque accent 16 %, marque accent 13×12,25, barre accent 10×2 sous le bouton.

### Bandeau

```cpp
struct BannerHooks { std::function<void()> exit; std::function<void(const std::wstring&)> announce; };
class Banner {
  void Init(HINSTANCE, HWND browserWindow, std::wstring appRoot, BannerHooks);
  bool Show(RECT bandClientPx, bool reducedMotion);   // 32 DIP sous la barre ; annonce « Mode Annotation activé »
  void Move(RECT bandClientPx);
  void Hide(bool animate = true);
  bool Visible() const; bool Created() const; HWND Window() const;
};
```
Fenêtre enfant du navigateur (au-dessus du WebView). L'hôte décale le WebView de 32 DIP (`Design::Vertical`) et route Échap (`AcceleratorKeyPressed`) vers sa propre sortie du mode ; « Quitter » (clic, Entrée/Espace/Échap si le bandeau a le focus, UIA Invoke) appelle `hooks.exit`.

### Popover de note

```cpp
struct Project { std::wstring id, label, origin; };
struct NoteContext { std::wstring page, element, text, styles, shotLabel; };   // styles: lignes séparées par \n
enum class CaptureState { Pending, Ready, Unavailable };
struct NoteRequest { std::wstring draft; std::vector<Project> projects; std::wstring projectId;
                     NoteContext context; CaptureState capture = Pending; bool includeShot = true, expanded = true; };
struct NoteSubmission { std::wstring note, projectId; bool includeShot; };
struct Shot { unsigned width, height; std::vector<uint32_t> pixels /*BGRA prémultiplié*/; D2D1_RECT_F mark; };
struct NoteHooks {
  std::function<void(const NoteSubmission&)> submit;                 // écrire HORS fil UI, puis Saved() ou Failed()
  std::function<void(const std::wstring& draft, bool keyboard)> cancel; // Échap / Annuler : l'hôte garde le brouillon
  std::function<void()> newProject;                                  // « Nouveau projet pour cette origine… »
  std::function<void(const std::wstring& projectId)> projectChanged;
  std::function<void()> blurred;                                      // perte d'activation (le popover reste ouvert)
  std::function<void(const std::wstring&)> announce;
};
class NotePopover {
  void Init(HINSTANCE, HWND browserWindow, std::wstring appRoot, NoteHooks);
  bool Show(const NoteRequest&, RECT targetScreenPx, RECT pageScreenPx, bool keyboard, bool reducedMotion);
  void SetShot(CaptureState, const Shot*);                          // capture finie / impossible (copie interne)
  void SetProjects(std::vector<Project>, const std::wstring& selectedId);
  void Saved();                                                      // écriture réussie : sortie animée
  void Failed(const std::wstring& message = …);                      // note conservée, « Réessayer » focalisé
  void Close(bool animate = true);
  bool Visible() const; HWND Window() const; HWND Edit() const;
};
```
Placement : sous la cible (8 DIP), sinon au-dessus, sinon à côté ; gardé à 8 DIP du bord de la page. Clavier : Entrée = Ajouter (Maj+Entrée = nouvelle ligne, Ctrl+Entrée partout), Échap ferme d'abord l'info-bulle ou la liste puis annule, Tab/Maj+Tab : champ → Projet → « Ce que l'agent verra » → info → « Inclure la capture » → Annuler → Ajouter. Projet : Bas/F4/Alt+Bas ouvre la liste (Haut/Bas/Début/Fin/Entrée/Échap). Validation à l'envoi (jamais de bouton désactivé) : note vide → erreur dans le champ ; aucun projet → erreur sur Projet.

### Lanceur + carte

```cpp
enum class LauncherState { Rest, Working };
enum class CardKind { None, Working, NoAgent, Added };
struct CardContent { CardKind kind; std::wstring agent, since, note, project, monogram; Shot avatar; };
struct LauncherHooks { std::function<void()> toggle, open, later, connect, focusOut;
                       std::function<void(bool keyboard)> dismissed; std::function<void(const std::wstring&)> announce; };
class Launcher {
  void Init(HINSTANCE, HWND browserWindow, std::wstring appRoot, LauncherHooks);
  void Place(RECT pageClientPx);                     // bounds du WebView ; ré-ancre lanceur (16/16) et carte (16/64)
  bool Show(LauncherState, int count, bool reducedMotion);
  void Hide(bool animate = true);
  void ShowCard(const CardContent&); void HideCard(bool animate = true);
  void FocusLauncher(); void FocusCard(); void SetBadgeRing(uint32_t rgba /*couleur de page, Paper #F4F4F3*/);
  bool Visible() const; bool CardVisible() const; HWND LauncherWindow() const; HWND CardWindow() const;
};
```
Deux fenêtres **enfants** à alpha par pixel : seuls les pixels dessinés reçoivent la souris (coins arrondis et pourtour du badge laissent passer le clic vers la page — vérifié par `WindowFromPoint`). Après un changement de z-order du WebView, rappeler `Place` (qui remet les surfaces en `HWND_TOP`).

### Onboarding

```cpp
enum class AgentApp { Claude, Codex, Other };
struct OnboardingSetup { std::wstring projectId, projectLabel, bridgePath, mcpbName = L"annotations-browser.mcpb", mcpbSize = L"1,2 Mo"; };
struct AgentRow { std::wstring monogram, name, detail; bool connected; };
struct VerifyStatus { bool read; std::wstring reader, when = L"il y a quelques secondes", detail; std::vector<AgentRow> agents; };
struct OnboardingHooks {
  std::function<void()> openMcpb;                           // l'hôte ouvre le .mcpb (ShellExecute) — rien n'est lancé ici
  std::function<void(const std::wstring&)> copy;            // l'hôte écrit le presse-papiers
  std::function<void(AgentApp)> verifyStarted, verifyStopped; // surveiller readers\ SEULEMENT entre les deux
  std::function<void(AgentApp)> help; std::function<void(int)> agentAction;
  std::function<void(bool completed)> closed; std::function<void(const std::wstring&)> announce;
};
class Onboarding {
  void Init(HINSTANCE, HWND browserWindow, std::wstring appRoot, OnboardingHooks);
  bool Show(const OnboardingSetup&, RECT browserClientScreenPx, bool reducedMotion, AgentApp app = AgentApp::Claude);
  void SetStatus(const VerifyStatus&);                       // depuis la surveillance de readers\
  void Close(bool animate = true, bool completed = false);
  bool Visible() const; HWND Window() const; HWND Veil() const;
};
// Textes purs (testés) :
std::wstring ClaudeConfig(path, projectId), CodexConfig(path, projectId), GenericConfig(path, projectId), ArgsText(projectId);
```
Modale : la fenêtre du navigateur est désactivée pendant l'ouverture (`EnableWindow`), réactivée avant la sortie ; le voile (`#0000008C`) avale les clics. Le navigateur n'écrit jamais la configuration d'une autre app : il affiche/copie.

## États

| Surface | États |
| --- | --- |
| Bandeau | visible (fondu), « Quitter » repos / survol / appui 0,96 / focus |
| Bouton « Annoter » | inactif (glyphe 50 %), survol, actif (plaque + barre accent), focus |
| Popover | saisie (champ focalisé, halo accent) · capture en cours (anneau + « Capture en cours… », saisie libre) · capture prête (miniature + repère accent) · capture indisponible (icône + « envoi sans image », case désactivée) · repli fermé · liste Projet ouverte · info-bulle · envoi (« Ajout… ») · échec d'écriture (message danger, « Réessayer », note intacte) · validation (note vide, projet manquant) |
| Lanceur | repos (bordure 12 %), en cours (bordure accent 40 %, anneau tournant, badge compteur), survol, appui, focus |
| Carte | en cours (WOL-0), aucun agent (WVJ-0 : Plus tard / Connecter un agent), annotation ajoutée (en-tête seul, part seule après 4 s) |
| Onboarding | 1 choisir l'app (radio + « Limiter au projet ») · 2 Claude (extension, méthode manuelle repliable, JSON) · 2 Codex (Commande/Arguments + config.toml) · 2 autre client (JSON `mcpServers`) · 3 attente (anneau, lien d'aide après 2 min) · 3 connecté (statut, liste des agents) · « Copié » sur le bouton utilisé |

## Mouvement (brief + apple-design + better-ui)

| Élément | Valeur |
| --- | --- |
| Ressort d'entrée | critique (amortissement 1,0), réponse 0,35 s : 98 % à 325 ms, posé à 557 ms |
| Ressort de sortie | critique, réponse 0,16 s : 98 % à **149 ms**, posé à 255 ms (même chemin, plus court) |
| Pose d'entrée | opacité 0→1, échelle 0,96→1, décalage 8 DIP (popover, vers la cible) / 10 DIP (carte, vers le lanceur) |
| Origines | popover : point du bord le plus proche de la cible ; carte : axe du lanceur sur le bord bas ; modale : centre ; voile = opacité × 0,549 |
| Interruption | reprise depuis la valeur **et la vitesse** présentées (mesuré à mi-course : 0,95956 → 0,95956, 0,605/s → 0,605/s) |
| Appui | 0,96 en 150 ms ease-out (0,9726 à 75 ms), jamais si désactivé ou mouvement réduit |
| Étapes onboarding | fondu croisé 200 ms ease-out, hauteur en ressort 0,35 s ; une interruption repart de l'alpha présenté |
| Anneau « en cours » | 1 tour / 1,2 s ; fixe en mouvement réduit |
| Mouvement réduit | même ressort, **opacité seule** (ni échelle ni glissement) ; popover : alpha de la vraie fenêtre, pas de fantôme |
| Rendu | popover : couche fantôme cliquable-à-travers pendant le mouvement, vraie fenêtre (EDIT focalisé dès l'image 0) à alpha 0 puis 255 à l'arrêt ; carte/lanceur/modale : dessin direct transformé |

Indice statique pour chaque état animé : couleur (accent/danger), icône (coche, anneau, danger) ou texte (« Capture en cours… », « Copié », « Vérifié »).

## Accessibilité

- UIA (`accessibility.h`) : bandeau (Groupe « Mode Annotation », statut « Activé », bouton « Quitter le mode Annotation », raccourci Échap) ; bouton barre (`ToolbarNode`, toggle, Ctrl+Maj+A) ; popover (Groupe « Nouvelle annotation » avec statut d'erreur, EDIT nommé « Remarque pour l'agent » + aide d'erreur, « Projet » expand/collapse, liste « Projets » d'éléments sélectionnables, « Ce que l'agent verra » expand, lignes de contexte en Texte avec valeur, « Inclure la capture » toggle/désactivé, Annuler/Ajouter avec raccourcis) ; lanceur « Annotations » (expand, aide « 1 annotation en cours ») ; carte (Groupe + Texte + boutons) ; onboarding (Groupe titré, Texte « Étape n sur 3 », liste « Application », case toggle, blocs de config en Texte avec valeur, boutons).
- Focus visible : anneau 1 DIP blanc (50 % clavier, 10 % pointeur, `Design::Focus`) sur chaque contrôle dessiné ; caret accent dans l'EDIT. Ordre Tab décrit plus haut ; Échap ferme partout ; la modale piège le focus (navigateur désactivé), le popover restaure le focus au retour d'activation.
- Annonces : « Mode Annotation activé », « Annotation ajoutée · Boutique », « Annotation ajoutée · Boutique. Aucun agent connecté. », « Annotation prise par … », erreurs de validation et d'écriture, « Projet … », « Copié », « Étape n sur 3 : … », « Codex a lu vos annotations ».

## Écarts assumés avec Paper

1. **Ombres portées non dessinées** (liste, info-bulle, lanceur, cartes, modale) : règle du dépôt « zero drop shadow » (`src/native-ui.h`, décision du 2026-11-09).
2. **JetBrains Mono** n'est ni embarquée ni installée sur la machine de test : repli Cascadia Mono puis Consolas (même 12/18, ligatures coupées). L'embarquer demande un changement de `tools/release/build.py` (hors périmètre T2).
3. Graisse **600** rendue en Medium (seuls Regular/Medium d'Inter Display sont embarqués ; pas de faux gras).
4. Logo du bandeau : le shader « Heatmap » est rendu par la même marque remplie de son dégradé (#D1DFFF → #3296FF → #2F63E7).
5. Champ de note : l'EDIT natif garde la hauteur de ligne GDI (~17 px au lieu de 20) ; halo du champ approché par anneaux dégradés.
6. Avatar de la carte « en cours » : monogramme par défaut, image si l'hôte fournit `CardContent::avatar`.
7. La ligne « Avant : … · après 2 min … » de XHN-0 est une note de conception : elle est implémentée comme comportement (attente animée, lien après 2 min).

## Preuves (harnais)

```
python tests/annotations-ui/run.py            # build + tests + captures + build/annotations-ui-qa/report.json
python tests/annotations-ui/run.py --no-window
```
Le harnais prend le premier plan (hôte topmost) pour tester les vrais clics : ne pas taper au clavier pendant les ~20 s de la partie fenêtres.

Résultat de référence (Windows 11 26200, 125 %) : **291 vérifications, 0 échec**. Sur une quinzaine de passes, trois ont eu 1–2 échecs intermittents (focus perdu, Échap absorbé par l'info-bulle ouverte au survol, un `WM_TIMER`) pendant que le poste était utilisé : le harnais force désormais le premier plan, le popover restaure son focus à la réactivation, et la vérification de repos imprime la classe de toute fenêtre recevant un `WM_TIMER` (seules nos surfaces font échouer). Les 8 dernières passes sont vertes.



### Échantillons de mouvement (carte, modèle pur)

| Instant | t (ms) | progression | opacité | échelle | dy (DIP) |
| --- | ---: | ---: | ---: | ---: | ---: |
| enter t=0 | 0.0 | 0.0000 | 0.0000 | 0.9600 | 10.000 |
| enter t=1/4 | 139.2 | 0.7127 | 0.7127 | 0.9885 | 2.873 |
| enter t=1/2 | 278.5 | 0.9596 | 0.9596 | 0.9984 | 0.404 |
| enter t=end | 557.0 | 1.0000 | 1.0000 | 1.0000 | 0.000 |
| exit t=1/2 | 127.5 | 0.0402 | 0.0402 | 0.9616 | 9.598 |
| interrupt +0 | 278.5 | 0.9596 | 0.9596 | 0.9984 | 0.404 |
| interrupt +40ms | 318.5 | 0.5178 | 0.5178 | 0.9807 | 4.822 |
| reduced t=1/2 | 278.5 | 0.9596 | 0.9596 | 1.0000 | 0.000 |

### Fenêtre réelle : entrée de la carte puis repos

Entrée : 594 ms, 67 images (horloge compositeur). Repos 1500 ms : images 0, requêtes d'horloge 0, réveils du thread 0, ticks 0, messages postés 0, WM_TIMER 0.

### Géométrie Paper (modèle de boîtes CSS) vs layout natif

| Surface | largeur | hauteur Paper | hauteur native |
| --- | ---: | ---: | ---: |
| note (WKR-0) | 340 | 536.00 | 536.00 |
| card no agent (WVJ-0) | 320 | 142.00 | 142.00 |
| card working (WOL-0) | 320 | — | 158.00 |
| onboarding choose (XAF-0) | 560 | 484.00 | 484.00 |

### Couleurs Paper échantillonnées dans les rendus (2x)

| Point | Paper (composé) | Rendu |
| --- | --- | --- |
| banner bottom border (#3E67FF3D over band) | `#23347D` | `#24347D` |
| toolbar active plate (accent 16 %) | `#1D243C` | `#1D233C` |
| toolbar active bar (accent) | `#3E67FF` | `#3E67FF` |
| note surface #292929 | `#292929` | `#292929` |
| note field #FFFFFF0A on raised | `#313131` | `#313131` |
| project selector #FFFFFF0A | `#313131` | `#313131` |
| context section #FFFFFF05 | `#2D2D2D` | `#2D2D2D` |
| « Annuler » #333333 | `#333333` | `#333333` |
| « Ajouter » accent #3E67FF | `#3E67FF` | `#3E67FF` |
| list hot row #FFFFFF14 | `#3A3A3A` | `#3A3A3A` |
| launcher surface #171717 | `#171717` | `#171717` |
| launcher border #FFFFFF1F | `#333333` | `#333333` |
| launcher surface #171717 | `#171717` | `#171717` |
| launcher border #3E67FF66 | `#273774` | `#273774` |
| badge accent | `#3E67FF` | `#3E67FF` |
| card surface #292929 | `#292929` | `#292929` |
| « Ouvrir » #FFFFFF0F | `#363636` | `#363636` |
| card surface #292929 | `#292929` | `#292929` |
| « Plus tard » #333333 | `#333333` | `#333333` |
| « Connecter un agent » accent | `#3E67FF` | `#3E67FF` |
| card surface #292929 | `#292929` | `#292929` |
| chosen option #3E67FF1A | `#2B2F3F` | `#2B303F` |
| other option #FFFFFF08 | `#303030` | `#303030` |
| footer #FFFFFF05 | `#2D2D2D` | `#2D2D2D` |
| code block #171717 | `#171717` | `#171717` |
| read status #3E67FF1F | `#313649` | `#323749` |
| veil #0000008C over the page | `#6E6E6E` | `#6E6E6E` |

### Contrastes mesurés (pixels rendus à 2x, puis paire de jetons)

| Texte | mesuré | jetons | encre | fond |
| --- | ---: | ---: | --- | --- |
| banner « Mode Annotation » #5B84FF | 4.87:1 | 4.86:1 | `#5B84FF` | `#191D36` |
| banner « Quitter » #FFFFFFE6 | 13.64:1 | 14.67:1 | `#E9E9EC` | `#191D36` |
| banner key cap « Échap » | 7.70:1 | 7.91:1 | `#C0C2CB` | `#2A2D3A` |
| note label 12px #FFFFFF99 | 6.19:1 | 6.22:1 | `#A9A9A9` | `#292929` |
| note text 14px #FFFFFFE6 | 10.85:1 | 10.83:1 | `#EBEBEB` | `#313131` |
| context row label 12px #FFFFFF99 | 5.98:1 | 5.99:1 | `#ABABAB` | `#2D2D2D` |
| context value mono 12px #FFFFFFCC | 9.36:1 | 9.36:1 | `#D5D5D5` | `#2D2D2D` |
| provider mention 12px #FFFFFF99 | 5.98:1 | 5.99:1 | `#ABABAB` | `#2D2D2D` |
| capture label 12px #FFFFFF99 | 5.98:1 | 5.99:1 | `#ABABAB` | `#2D2D2D` |
| « Ajouter » white on accent | 4.58:1 | 4.58:1 | `#FFFFFF` | `#3E67FF` |
| « Annuler » white on #333333 | 12.63:1 | 12.63:1 | `#FFFFFF` | `#333333` |
| project origin 13px #FFFFFF99 | 6.19:1 | 6.22:1 | `#A9A9A9` | `#292929` |
| tooltip 13px #FFFFFFE6 on #171717 | 14.63:1 | 14.67:1 | `#E8E8E8` | `#171717` |
| write error 12px #FF7B6E | 5.76:1 | 5.76:1 | `#FF7B6E` | `#292929` |
| card « Depuis 2 min » 14px #FFFFFF99 | 6.19:1 | 6.22:1 | `#A9A9A9` | `#292929` |
| card project 12px #FFFFFF99 | 6.19:1 | 6.22:1 | `#A9A9A9` | `#292929` |
| card « · Boutique » 13px #FFFFFF99 | 6.19:1 | 6.22:1 | `#A9A9A9` | `#292929` |
| card body 14px #FFFFFFE6 | 12.09:1 | 12.10:1 | `#EAEAEA` | `#292929` |
| modal subtitle 14px #FFFFFFB3 | 7.91:1 | 7.93:1 | `#BFBFBF` | `#292929` |
| footer note 12px #FFFFFF99 | 5.98:1 | 5.99:1 | `#ABABAB` | `#2D2D2D` |
| option description 13px #FFFFFF99 | 5.84:1 | 5.84:1 | `#ACACAC` | `#303030` |
| todo step label 12px #FFFFFF99 | 6.19:1 | 6.22:1 | `#A9A9A9` | `#292929` |
| config mono 12px #FFFFFFCC | 11.74:1 | 11.69:1 | `#D1D1D1` | `#171717` |
| .mcpb label 12px #FFFFFF99 | 5.84:1 | 5.84:1 | `#ACACAC` | `#303030` |
| « Ça ne marche pas ? » #9DB3FF | 5.78:1 | 5.78:1 | `#9DB3FF` | `#383838` |
| waiting status 13px #FFFFFFB3 | 6.67:1 | 6.71:1 | `#C3C3C3` | `#383838` |
| read detail 13px #FFFFFFB3 on accent 12 % | 6.75:1 | 6.77:1 | `#C1C3C8` | `#313649` |
| agent detail 12px #FFFFFF99 | 6.19:1 | 6.22:1 | `#A9A9A9` | `#292929` |

Police mono résolue : Consolas.

### Captures (`build/annotations-ui-qa/`)

`01-banner.png`, `01b-banner-quit-focus.png`, `02b-toolbar-annotate-idle.png`, `02-toolbar-annotate-active.png`, `03-note-editing.png`, `04-note-collapsed.png`, `05-note-project-list.png`, `06-note-tooltip.png`, `07-note-capturing.png`, `08-note-write-failed.png`, `09-note-empty-validation.png`, `09b-note-capture-unavailable.png`, `10-launcher-rest.png`, `11-launcher-working.png`, `12-card-working.png`, `13-card-no-agent.png`, `14-card-added.png`, `15-onboarding-choose.png`, `16-onboarding-claude.png`, `17-onboarding-claude-manual.png`, `18-onboarding-codex.png`, `18b-onboarding-codex-toml.png`, `19-onboarding-other.png`, `20-onboarding-verify-waiting.png`, `21-onboarding-verify-help.png`, `22-onboarding-verify-connected.png`, `30-scene-v2-note.png`, `31-scene-v5-working.png`, `32-scene-onboarding-1-no-agent.png`, `33-scene-onboarding-2-choose.png`, `34-scene-onboarding-3a-claude.png`, `35-scene-onboarding-4-connected.png`, `40-motion-card-enter-0-quarter-half-end.png`, `41-motion-card-exit-0-quarter-half-end.png`, `42-motion-card-interrupt-mid.png`, `43-motion-card-reduced.png`, `44-motion-note-enter-from-target.png`, `45-motion-modal-enter.png`, `46-motion-onboarding-step-crossfade-mid.png`, `50-window-banner.png`, `51-window-launcher-rest.png`, `52-window-launcher-working.png`, `53-window-card-entering.png`, `54-window-card-no-agent.png`, `55-window-note-ghost-entering.png`, `56-window-onboarding-choose.png`, `57-window-onboarding-crossfade.png`, `58-window-onboarding-connected.png`
