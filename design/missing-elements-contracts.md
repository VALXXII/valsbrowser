# Éléments manquants — contrats d'intégration (lead : main.cpp)

Source de vérité : Paper « Browser Design » (fileId `01M6Q7YT14ZKMGET54ASPNGQEC`), page `p-D-0`
« Éléments & popups — conception complète », 28 artboards A1–A9, B01–B09, C0–C9.
Ne jamais modifier Paper. Valeurs exportées via `get_jsx` / `get_computed_styles`, jamais lues sur une capture.

## Propriété des fichiers (un seul éditeur par fichier)

| Propriétaire | Fichiers |
| --- | --- |
| **Lead (intégrateur)** | `src/main.cpp`, `src/elements-integration.h` (partie de main.cpp : plomberie WebView2), `src/chrome.h`, `src/accessibility.h`, `src/browser.rc`, `tools/release/*`, `build.ps1`, `README.md`, `test.ps1`, `tests/run_all.py` s'il existe, ce fichier, `design/missing-elements-mapping.md` (assemblage final) |
| **A — fondation modales / popovers / zéro ombre** | `src/native-ui.h`, `src/design-system.h`, `src/modal.h` (nouveau), `src/origin-prompt.h` (nouveau), `src/password-demo.h` (nouveau) ; *uniquement pour appliquer le helper sans ombre* : `src/bookmark-toast.h`, `src/bookmark-popover.h`, `src/bookmarks-panel.h`, `src/context-menu.h`, `src/download-panel.h`, `src/history-panel.h`, `src/settings-panel.h`, `src/address-popup.h`, `src/update-toast.cpp`, `src/status-bubble.h`, `src/split-overlay.h` ; tests `tests/popup_shadow_test.cpp`, `tests/modal_test.cpp`, `test-popups.ps1` ; doc `design/missing-elements-A.md` |
| **B — sandbox, recherche, erreurs, toasts, info-bulles, plein écran** | `src/find-bar.h`, `src/error-pages.h`, `src/context-toast.h`, `src/tooltip.h`, `src/fullscreen-hint.h`, `src/debug-sandbox.h` (tous nouveaux) ; tests `tests/debug_sandbox_test.cpp`, `tests/error_pages_test.cpp`, `test-sandbox.ps1` ; doc `design/missing-elements-B.md` |

Besoin d'un fichier d'un autre propriétaire (ex. un token dans `design-system.h` pour B) : demander au propriétaire via `orca orchestration send`, ne pas l'éditer. B peut définir des constantes locales dans ses propres en-têtes en attendant.

## Conventions communes

- Composants natifs en-tête seul, namespace `NativeChrome::<Nom>`, sur le modèle de `bookmark-popover.h` / `bookmark-toast.h` : `Create(HINSTANCE, HWND owner, const std::wstring& root, Hooks)`, `Dispose()`, `Visible()`, `Window()`, `NextWake(Motion::Time)`, `Frame(Motion::Time)`, `SetReducedMotion(bool)`. Rendu Direct2D via `NativeChrome::Chrome` existant, tokens `design-system.h`, motion `motion.h`, UI Automation via `accessibility.h` (`Accessibility::Bridge`/`Host`, comme bookmark-toast).
- Aucun appel WebView2 dans les composants (sauf types d'énumération si utile) : main.cpp branche le SDK.
- Callbacks `done` appelés **exactement une fois** (bouton, Échap, `Abort()`, propriétaire détruit). Après `done`, le composant n'appelle plus rien.
- Clavier : Échap = annuler, Entrée = bouton par défaut (sauf champ multiligne), Tab/Maj+Tab cyclent dans le composant, focus restauré à la fermeture (le composant mémorise `GetFocus()` au `Show` ; main peut ensuite déplacer le focus page).
- Feedback au pointer-down, interactions fréquentes instantanées (≤150 ms opacité/fond), entrée/sortie discrètes non bloquantes, interruptibles ; `SetReducedMotion(true)` ⇒ fondu d'opacité seul. DPI par fenêtre (`GetDpiForWindow`) et `WM_DPICHANGED`.
- **Zéro ombre portée** (voir ci-dessous). Les anneaux de focus inset et contours restent.
- Textes en français, typographie Inter Display, valeurs Paper exactes.

## Zéro ombre (A fournit, tous appliquent)

`src/native-ui.h` (A) expose, signatures figées :

```cpp
namespace NativeUI {
// À appeler à la création de chaque fenêtre popup à nous, et de nouveau sur
// WM_DPICHANGED / WM_NCACTIVATE / WM_SHOWWINDOW(TRUE) (idempotent, bon marché).
void ConfigureShadowlessPopup(HWND window);
// Vérification (tests/QA) : true si ni CS_DROPSHADOW, ni WS_CAPTION/WS_THICKFRAME,
// ni rendu NC DWM susceptible de dessiner une ombre.
bool IsShadowless(HWND window);
// Info-bulles comctl (classe tooltips_class32, enregistrée par processus) : retire
// CS_DROPSHADOW de la classe pour CE processus uniquement.
void StripTooltipShadow(HWND tooltip);
}
```

`RoundNatively` est révisé par A (l'ancienne acceptation de l'ombre DWM est révoquée). Aucun hack global, DWM reste actif, aucune ombre peinte.

## A — API attendues (ajustables ; l'en-tête final fait foi, prévenir le lead)

```cpp
namespace NativeChrome::Modal {
enum class Kind { Generic, Alert, Confirm, Prompt, BeforeUnload, ClearData, CloseWindow, OpenManyTabs, DeleteBookmarks, About, Private };
enum class Role { Primary, Secondary, Danger };
struct Button { int id; std::wstring label; Role role = Role::Secondary; bool isDefault = false, isCancel = false, disabled = false; };
struct Field  { std::wstring label, value, placeholder, error; bool password = false, required = false; };
struct Spec {
  Kind kind = Kind::Generic; std::wstring automationId, title, body, origin /* « exemple.com indique » */, footnote;
  std::vector<Field> fields; std::wstring checkLabel; bool checkValue = false;
  std::vector<Button> buttons; bool demo = false; // badge « Démonstration »
  std::function<std::wstring(const std::vector<std::wstring>&)> validate; // erreur inline ou ""
};
struct Result { int button = -1; /* -1 : annulé/abandonné */ std::vector<std::wstring> values; bool checked = false; };
class Dialog {
public:
  struct Hooks { std::function<void()> wake; /* demande une frame à l'horloge hôte */ };
  bool Create(HINSTANCE, HWND owner, const std::wstring& root, Hooks);
  void Dispose();
  // `area` : rectangle écran (px) sur lequel centrer / poser le voile (zone page ou fenêtre).
  bool Show(Spec, RECT area, std::function<void(const Result&)> done); // false si déjà ouverte
  void Abort();                 // done({-1}) puis fermeture
  void SetBusy(bool busy, const std::wstring& label = L""); // A5 « Effacement… »
  void Reposition(RECT area);
  bool Visible() const; HWND Window() const;
  Motion::Wake NextWake(Motion::Time) const; void Frame(Motion::Time); void SetReducedMotion(bool);
};
// Préréglages (contenus A2…A9 d'après Paper) :
Spec AlertSpec(const std::wstring& origin, const std::wstring& message);
Spec ConfirmSpec(const std::wstring& origin, const std::wstring& message);
Spec PromptSpec(const std::wstring& origin, const std::wstring& message, const std::wstring& defaultText);
Spec BeforeUnloadSpec(const std::wstring& origin, bool reload);
Spec ClearDataSpec(); Spec CloseWindowSpec(int tabs); Spec OpenManyTabsSpec(int count); Spec DeleteBookmarksSpec(int count);
Spec AboutSpec(const std::wstring& version, /* état A8 */ int state); Spec PrivateSpec(bool concept);
}
namespace NativeChrome::OriginPrompt {
enum class Kind { Permission, BasicAuth, ExternalApp, DangerousDownload, ClientCertificate, ScreenShare, KeepFile, PasswordSave, PasswordUpdate, PasswordFill };
enum class Action { Dismiss, Allow, AllowOnce, Block, Submit, Cancel, Open, Keep, Delete, Choose };
struct DemoAccount { std::wstring user, hint; };
struct Spec { Kind kind; std::wstring origin, detail; std::vector<int> permissions; /* valeurs COREWEBVIEW2_PERMISSION_KIND */
              bool userInitiated = true, demo = false; std::vector<DemoAccount> accounts; std::wstring user; };
struct Result { Action action = Action::Dismiss; bool remember = false; std::wstring user, password; int choice = -1; };
class Prompt { public:
  struct Hooks { std::function<void()> wake; };
  bool Create(HINSTANCE, HWND owner, const std::wstring& root, Hooks); void Dispose();
  // `anchor` : rectangle écran (px) de l'indicateur d'origine de la barre d'adresse.
  bool Show(Spec, RECT anchor, std::function<void(Result&)> done); // le mot de passe est effacé (SecureZeroMemory) après done
  void Abort(); void Reposition(RECT anchor); bool Visible() const; HWND Window() const;
  Motion::Wake NextWake(Motion::Time) const; void Frame(Motion::Time); void SetReducedMotion(bool);
}; }
// password-demo.h : NativeChrome::PasswordDemo — B05 (liste, détail, suppression), comptes fictifs en mémoire uniquement.
```

## B — API attendues

```cpp
namespace NativeChrome::FindBar {
struct Options { bool matchCase = false, wholeWord = false; };
enum class State { Idle, Searching, Found, NoMatch, Unavailable };
struct Hooks { std::function<void(const std::wstring&, const Options&)> query; std::function<void()> next, previous, close, wake; };
class Bar { public: bool Create(HINSTANCE, HWND owner, const std::wstring& root, Hooks); void Dispose();
  void Show(RECT pageScreen, const std::wstring& seed); void Hide(); void Focus(); bool HasFocus() const;
  void SetResult(int active /* 1-based, 0 = aucun */, int count, State); void Reposition(RECT pageScreen);
  std::wstring Text() const; bool Visible() const; HWND Window() const;
  Motion::Wake NextWake(Motion::Time) const; void Frame(Motion::Time); void SetReducedMotion(bool); }; }
namespace NativeChrome::ErrorPages {
enum class Kind { Offline, NameNotResolved, Refused, TimedOut, Reset, Unreachable, Generic };
Kind Classify(int webErrorStatus /* COREWEBVIEW2_WEB_ERROR_STATUS */, bool online);
std::wstring Html(Kind, const std::wstring& url, const std::wstring& token, bool retrying);
enum class Action { Retry, Back, Details };
std::optional<Action> ParseMessage(const std::wstring& json, const std::wstring& token); }
namespace NativeChrome::ContextToast { // A7 limite d'onglets, C8, C9
struct Spec { std::wstring automationId, message, action; int icon = 0; bool error = false; int durationMs = 4000; };
class Toast { /* Create/Dispose/Show(Spec, RECT pageScreen, std::function<void(bool actioned)>)/Hide/Reposition/Visible/Window/NextWake/Frame/SetReducedMotion */ }; }
namespace NativeChrome::Tooltip { // C5 : remplace TOOLTIPS_CLASS là où main l'utilise
class Tip { /* Create/Dispose/Show(text, shortcut, RECT targetScreen)/Hide/Visible/Window/SetReducedMotion */ }; }
namespace NativeChrome::FullscreenHint { // C6/C7
class Hint { /* Create/Dispose/Show(RECT monitorScreen, bool pageFullscreen /* Échap */ vs browser F11)/Hide/Visible/Window/NextWake/Frame/SetReducedMotion */ }; }
namespace NativeChrome::DebugSandbox {
inline constexpr wchar_t Host[] = L"browser-debug.invalid"; // servi par main via WebResourceRequested (jamais le réseau)
inline constexpr wchar_t Url[]  = L"https://browser-debug.invalid/sandbox";
std::string Html(const std::wstring& token);           // UTF-8, catalogue français complet
struct Command { std::wstring name; std::wstring variant; }; // name ∈ allowlist
std::optional<Command> ParseCommand(const std::wstring& json, const std::wstring& token); // allowlist stricte
const std::vector<std::wstring>& AllowedCommands();
std::wstring ResultScript(const std::wstring& line); // JS appelé par main via ExecuteScript/PostWebMessage pour le journal
}
```

## Sécurité du pont sandbox (main + B)

- Page servie uniquement à l'onglet ouvert par l'utilisateur (`browser://debug`, menu, `--debug-sandbox`) ; main annule toute navigation vers l'hôte sandbox qu'il n'a pas lui-même lancée.
- Message accepté seulement si : onglet marqué sandbox, `get_Source` == `Url` exact (top-level ; les iframes n'émettent pas sur `ICoreWebView2::WebMessageReceived`), jeton aléatoire par lancement correct, commande dans l'allowlist.
- Commandes non destructrices : aucune n'efface de données ni ne ferme la fenêtre réellement ; « Effacer les données » / « Fermer la fenêtre » ouvrent la vraie modale en mode **Démonstration** dont la confirmation ne fait que journaliser.
- Mots de passe : comptes fictifs en mémoire, libellés « Démonstration », jamais persistés ni lus depuis une page.

## Jalons à signaler au lead (orca orchestration send)

1. API d'en-tête stabilisée (compile seule) → le lead commence l'intégration.
2. Composants complets + tests verts.
3. worker_done avec fichiers modifiés et commandes de test.
