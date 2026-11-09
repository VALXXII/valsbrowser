# Aperçu d'URL au survol — correspondance Paper ↔ code

Page Paper : [Aperçu URL — composant](https://app.paper.design/file/01M6Q7YT14ZKMGET54ASPNGQEC/p-B-0)
Source de vérité : la frame **« Aperçu URL — contexte bas gauche »** (design de
l'utilisateur, calque « État · URL longue — largeur standard (1/3) » dans la
Viewbox). La frame isolée « Aperçu URL — composant » est l'ancienne
reconstitution de la bulle Chromium, elle n'est plus la référence.

## Valeurs Paper retenues (get_jsx / get_computed_styles)

| Propriété | Paper | Code (`src/status-bubble.h`, `Layout`) |
|---|---|---|
| Position | `absolute; left:0; bottom:0` dans la Viewbox (page) | collée au coin bas gauche du panneau survolé (`Tab::placed`) |
| Hauteur | 23 px | `height = 23` DIP |
| Padding | `paddingInline: 6px` | `padX = 6` |
| Fond | `#171717` | `surface = 23/255` (= token canvas) |
| Rayon | `borderTopRightRadius: 8px`, autres coins carrés | coin haut droit 8, autres carrés |
| Largeur | `width/maxWidth: 393px` | s'ajuste au texte, plafonnée à 393 DIP et à la largeur du panneau |
| Texte | Inter Display Regular 14, `letterSpacing 0.01em`, `#FFFFFF` à `opacity 0.5`, une ligne, `text-overflow: ellipsis` | DirectWrite Inter Display 400 14, espacement 0,14 DIP, blanc 50 %, sans retour à la ligne, ellipse caractère |
| Bordure / ombre | aucune | aucune |

Seul écart volontaire : Paper fige la largeur à 393 px (cas d'une URL longue) ;
une URL courte donne une bulle qui épouse son texte au lieu d'une bande vide de
393 DIP. Le coin bas gauche arrondi de la Viewbox (8 dans Paper, 6 DIP dans
l'app) n'est pas redessiné : la bulle est de la même couleur que le canvas, le
coin de fenêtre reste propre.

## Implémentation

- `src/status-bubble.h` : `NativeChrome::StatusBubble::Bubble`, popup
  `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE` possédée par la fenêtre
  principale (les pages WebView2 sont des fenêtres enfants : rien dessiné par la
  fenêtre principale ne peut les recouvrir). Rendu Direct2D (DC render target,
  alpha par pixel, coins antialiasés) + DirectWrite. Transparente à la souris :
  elle ne vole jamais un clic. Surface libérée quand elle est masquée.
- `src/main.cpp` :
  - à la création de chaque WebView : `ICoreWebView2_12::add_StatusBarTextChanged`
    puis `put_IsStatusBarEnabled(FALSE)` (la bulle du moteur n'est coupée que si
    l'événement est disponible ; sinon log `STATUS …` et bulle moteur conservée) ;
  - `Tab::status` garde le texte, `SetTabStatus` / `SyncStatusBubble` affichent la
    bulle sur le seul panneau survolé ;
  - masquage : texte vide (fin de survol), `NavigationStarting`, onglet ou panneau
    retiré de l'écran (changement d'onglet, split view, fermeture : via
    `ApplyPages`), fenêtre réduite ; repositionnement sur `WM_MOVE`, taille/DPI via
    `ApplyPages`/`Layout`.

## Vérifications

- `python tools/release/build.py --out build/v2` : compilation OK, sans avertissement.
- `test-status-bubble.ps1` (`tests/ui/status_bubble_test.cpp`, sans WebView2) :
  hauteur 23, ancrage bas gauche, largeur ajustée (URL courte), 393 (URL longue),
  plafonnée au panneau (250), fond #171717 sur trois coins carrés, coin haut droit
  transparent, texte à 50 % (gris 139 sur #171717), padding gauche, styles
  click-through, masquage sur texte vide. Preuve : `url-preview-qa/fixture-long-3x.png`.
- Contrôle visuel dans l'app réelle (DPI 125 %, pages locales, curseur réel) :
  split view, survol à gauche → bulle sur le panneau gauche
  (`url-preview-qa/split-hover-left.png`), à droite → panneau droit seulement
  (`split-hover-right.png`), hors lien → aucune bulle, ni la nôtre ni celle du
  moteur (`split-no-link.png`). Onglet simple vérifié de la même façon.
- Régressions : `test-split-view.ps1`, `test-ui.ps1`, `test-hover-favicon.ps1` passent.

Non vérifié en direct : changement/fermeture d'onglet pendant un survol (couvert
par le code via `ApplyPages`), et le cas d'un lien situé sous la bulle (elle le
recouvre visuellement mais le clic passe).

## Modifier le design

Pas de synchronisation automatique. Éditer la frame de contexte dans Paper, relire
les valeurs avec `get_jsx` / `get_computed_styles`, reporter dans
`StatusBubble::Layout`, puis relancer `test-status-bubble.ps1` (ses attentes
reprennent ces valeurs).
