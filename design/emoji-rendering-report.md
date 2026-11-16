# Emojis en couleur dans l'interface native — rapport

Branche `VALXXII/bookmark-emoji-render`, base `main` 4e37034. Commits locaux, rien de poussé :

| Commit | Contenu |
| --- | --- |
| `dca5636` | Source : `src/emoji-text.h` (nouveau), `src/chrome.h` (+3/-3), `src/tooltip.h` (+12/-5) |
| `2e6d4aa` | Tests : `tests/ui/emoji_text_test.cpp`, `tests/bookmarks/bookmark_emoji_ui_test.cpp`, une étape dans `test-ui.ps1` et `test-bookmarks-ui.ps1` |
| (commit preuves) | Ce rapport et `design/emoji-rendering-evidence/` (journaux `.txt`, captures PNG) |

Avant / après, barre de favoris réelle à 144 DPI (`captures/compare-band-144-before-top-after-bottom.png`) : en haut le rendu de main (soleil, outils, livres, note, ballon en glyphes blancs), en bas le correctif (vrais emojis colorés, atténués comme le libellé au repos).

## Cause (prouvée, `00-diagnostic-probe-base.txt`)

Reproduction minimale hors écran avec la vraie `Chrome::MakeTextLayout` de main, 96 DPI, fond #171717. Trois causes indépendantes :

1. **Option de dessin.** `Chrome::Text` et `PopupKit::DrawText/DrawRich` appellent `DrawTextLayout(..., D2D1_DRAW_TEXT_OPTIONS_NONE)`. Le repli de police du système choisit déjà « Segoe UI Emoji » pour U+1F6E0, U+1F600, ZWJ, tons de peau, mais sans `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT` Direct2D dessine le contour monochrome de ces glyphes. Avec l'option : 0 → 137 pixels colorés (U+1F600), 0 → 114 (ZWJ), 0 → 99 (ton de peau), texte Inter identique (200/200 pixels d'encre).
   Source : [D2D1_DRAW_TEXT_OPTIONS](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/ne-d2d1-d2d1_draw_text_options) — « In Windows 8.1 and later, text is rendered using color versions of glyphs, if defined by the font. »
2. **Inter capture certains symboles.** Inter Display contient lui-même U+2600 (soleil), U+2764, U+2665, U+26A0, U+2B1C, des flèches (U+2194–2199, U+25B6, U+25C0…). Le repli DirectWrite se fait caractère par caractère selon la couverture et ignore le sélecteur de variante : « soleil + FE0F » reste le glyphe monochrome d'Inter, même avec l'option couleur. Forcer la police emoji du système sur la grappe le colore (0 → 148 pixels).
3. **Opacité.** Les calques des glyphes couleur ignorent l'alpha du pinceau : un emoji dessiné à opacité 0 reste visible à 100 % (encre 174 à opacité 1, 0,3 et 0). Les libellés au repos (50 %), les onglets inactifs et les rangées estompées auraient donc des emojis à pleine opacité.

## Politique de présentation (choix A validé par le coordinateur)

Présentation Unicode respectée (UTS #51), chaîne jamais modifiée : seules des plages de police de la mise en page sont posées.

| Grappe | Police | Exemple |
| --- | --- | --- |
| base + FE0F, Emoji_Presentation=Yes, séquence ZWJ / ton de peau / keycap / tag | police emoji du système (« Segoe UI Emoji »), toute la séquence | soleil+FE0F, cœur+FE0F, étoile U+2B50, femme+ZWJ+ordinateur, #+FE0F+20E3 |
| base + FE0E | texte : Inter s'il couvre la base, sinon « Segoe UI Symbol » | soleil+FE0E (Inter), outils+FE0E (Segoe UI Symbol) |
| symbole à présentation texte par défaut, sans sélecteur | inchangé (Inter s'il le couvre, sinon repli normal) | **soleil nu U+2600 : reste le glyphe texte d'Inter** |
| emoji non-BMP sans sélecteur | repli normal (déjà la police emoji) | outils U+1F6E0 nu, U+1F600 |

Distinction pour la capture de l'utilisateur : si le titre stocké contient le soleil **avec** FE0F (cas du panneau emoji de Windows et des copies web habituelles), il devient coloré ; s'il contient le soleil **nu**, il reste un glyphe texte, conformément à Unicode et à ce que fait un navigateur avec une page en Inter. Les outils U+1F6E0 sont colorés dans les deux cas (Inter ne les couvre pas).

Garde-fous : une famille absente ou qui ne couvre pas la base laisse la grappe au repli normal (jamais de glyphe vide). Une grappe FE0E qu'aucune police texte ne couvre (emojis récents sans variante texte Unicode, mesuré : aucun caractère BMP concerné ; 298 non-BMP récents) fait dessiner toute la ligne sans couleur plutôt que de contredire FE0E en silence. Seules les grappes commençant par un symbole, `#`, `*`, chiffre, ©, ® sont considérées : un ZWJ dans du devanagari ou de l'arabe n'est jamais touché (testé).

## Correctif (`dca5636`)

`src/emoji-text.h` (pas de nouvelle dépendance, pas de pack d'emojis) :

- `Presentation(s)` : fonction pure qui renvoie les grappes à forcer (indices UTF-16, paires de substitution entières).
- `Prepare(write, layout, s, fonts, "Inter Display")` : filtre bon marché (`ColorCandidate` : aucun caractère ≥ U+2100 hors zone privée, aucune paire → retour immédiat, donc le texte courant ne coûte qu'un balayage) ; sinon applique la politique, puis vérifie **exactement** si un glyphe couleur sera dessiné (`Colored` : passe la mise en page dans un renderer qui lit `IDWriteFontFace2::IsColorFont` de chaque run) ; et garde la boîte de ligne d'Inter (`KeepLineBox`) pour qu'un libellé avec emoji garde la ligne de base de ses voisins (avant : 16,94 → 18,62 DIP de hauteur, décalage de 0,85 DIP).
- `Draw(...)` : `ENABLE_COLOR_FONT` ; si la ligne ne contient pas de couleur, chemin d'origine (alpha dans le pinceau, pixels identiques) ; si elle en contient et que l'alpha < 1, alpha appliqué **une seule fois** à toute la ligne via une couche (`PushLayer` sans ressource, [Windows 8+](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1rendertarget-pushlayer(constd2d1_layer_parameters__id2d1layer))), pinceau opaque ; opacité 0 → rien.

Branchements : `Chrome::MakeTextLayout` (le résultat est mis dans `TextMeasure`, donc dans le cache de texte existant : calculé une fois par chaîne et taille) et `Chrome::Text` ; `PopupKit::MakeLayout` (paramètre optionnel), `DrawText`, `DrawRich` (couche à l'alpha le plus grand, pinceaux normal et accentué divisés par celle-ci : chaque run garde son alpha exact).

## Portée de chaque primitive

| Primitive | Surfaces | État |
| --- | --- | --- |
| `Chrome::Text` / `TextWidth` / `MeasureText` | barre de favoris, menus natifs (dossiers, sous-menus imbriqués, », menus contextuels), tiroir Favoris, titres d'onglets, historique, téléchargements, réglages… | corrigé, testé (barre, menus, sous-menu, », tiroir, onglets) |
| `PopupKit::DrawText` / `DrawRich` / `Measure` | menu profil (noms, titres d'onglets), toasts (favori supprimé…), info-bulles, bandeau plein écran | corrigé, testé hors écran |
| Appels directs restants, **non modifiés** | `split-overlay.h` (titres pendant le glisser de la vue partagée), `modal.h` (textes de dialogue), `status-bubble.h` (URL survolée), `update-toast.cpp`, `annotation-kit.h` | emojis encore monochromes là ; hors de la cible (favoris), non étendus volontairement. Corrigeables en une ligne chacun avec `EmojiText::Prepare` + `Draw` si souhaité |
| Champs d'édition GDI (renommage en place, popover favori, barre d'adresse) | contrôles EDIT Windows | inchangés : emojis monochromes pendant l'édition (limite du contrôle) |

Icônes Phosphor, dossiers et favicons : non touchés (ce sont des bitmaps, pas du texte).

## Tests (commandes et résultats)

| Commande | Résultat | Journal |
| --- | --- | --- |
| `emoji_text_test` sur `src/` de 4e37034 | **ROUGE** : 57/144 échecs (0 pixel coloré partout, boîte de ligne 16,94 → 18,62, PopupKit 0) | `01-…-RED-base-4e37034.txt` |
| `emoji_text_test` sur `2e6d4aa` | **VERT** : 147/147 (96/144/192 DPI) | `02-…-GREEN.txt` |
| `bookmark_emoji_ui_test` sur `src/` de 4e37034 | **ROUGE** : 15 échecs (barre, menu, sous-menu, », tiroir × 3 DPI : 0 pixel coloré ; le tiroir n'a que ~40 pixels du curseur bleu) | `03-…-RED-base-4e37034.txt` |
| `bookmark_emoji_ui_test` sur `2e6d4aa` | **VERT** : 0 échec, 0 UNAVAILABLE (ex. 144 DPI : barre 1202, menu 713, sous-menu 529, » 1065, tiroir 1752 pixels colorés) | `04-…-GREEN.txt` |
| `test-ui.ps1` (inclut le nouveau test) | vert, exit 0 | `05-test-ui-ps1.txt` |
| `test-popups.ps1 -SkipPixels` | modal_test 278 contrôles, 3 échecs « période » **identiques sur 4e37034** (préexistants, sans lien) ; captures bureau non lancées (interdit) | `06a`, `06b` |
| `check_strings.py` + `i18n_test` | 0 constat ; 78 contrôles, 0 échec | `07-i18n.txt` |
| en-têtes seuls, `-Werror` | `emoji-text.h`, `chrome.h` propres ; `tooltip.h` : 2 avertissements d'indentation déjà présents sur main | `08-header-hygiene.txt` |
| `tools/release/build.py --out build/emoji-app` | Build OK 0.3.380, 0 avertissement | `09-build.txt` |
| `inline_rename_test`, `bookmark_rename_test` | PASS / PASS | `10-bookmark-rename-tests.txt` |

Contenu des contrôles pixel (seuil : chroma max−min > 32 ; tout le chrome est gris, seuls les glyphes couleur en produisent) : chaînes de la capture (soleil+FE0F coloré, soleil nu texte, soleil+FE0E texte, outils nu/FE0F colorés, outils+FE0E texte), U+1F600, cœur+FE0F, étoile, ZWJ, ZWJ avec FE0F interne, ton de peau, keycap ; une ligne mêlant FE0E/FE0F BMP et non-BMP mesurée grappe par grappe ; texte français ordinaire **identique au pixel** au dessin d'avant à 1 / 0,64 / 0,5 / 0,32 ; emoji à 32 % → luminosité 0,32, à 0 % → invisible, couleur conservée ; ligne mixte à 50 % : partie texte à ≤ 2 niveaux du texte seul, emoji à 0,50 ; largeur ZWJ = un glyphe ; titre tronqué : rien hors de sa case ; `DrawRich` : run accentué à 1,000 de son alpha ; barre de production au repos à 50 % : emoji à 0,50 de l'opaque.

Conditions de QA : données synthétiques uniquement, aucun profil lu ou écrit (`smoke`), pas de police ni réglage Windows modifié, aucun `SendInput`, `SetForegroundWindow` intercepté (9 demandes de production enregistrées puis ignorées, comme `bookmark_rename_test`), DPI simulé par `GetDpiForWindow` du test, captures `PrintWindow` de la fenêtre du test uniquement (capture noire → UNAVAILABLE, aucune ici). `bookmarks_ui_test` (dans `test-bookmarks-ui.ps1`) n'a **pas** été lancé : il ouvre le tiroir sans intercepter `SetForegroundWindow`. `run_all` non lancé (demandé après revue).

## Captures (`design/emoji-rendering-evidence/captures/`)

- `compare-band-144-before-top-after-bottom.png`, `compare-menu-folder-144-before-left-after-right.png`
- `before-*` (main) et `after-*` (correctif) : barre 144/192, menu dossier, sous-menu, » à 560 DIP, tiroir ; barre hors écran au repos/survol ; cas texte (soleil FE0F, soleil nu, outils FE0E, sélecteurs mélangés, troncature, drapeau, PopupKit).

## Limites honnêtes

- **Drapeaux** : Segoe UI Emoji n'a pas d'illustrations de drapeaux ; FR s'affiche en deux petites lettres (jamais vide). Limite de Windows, aucun pack ajouté.
- **Soleil nu** et autres symboles texte par défaut couverts par Inter (cœur, attention…) restent des glyphes texte (choix A).
- Une grappe FE0E sans aucune police texte rend toute la ligne monochrome (cas théorique, aucun caractère BMP concerné).
- `DrawRich` : un emoji prend l'alpha le plus grand des deux runs de la ligne.
- Contraste élevé : le chrome Direct2D ne réagit pas aux couleurs forcées (déjà le cas avant) ; les emojis restent colorés, comme dans Edge/Chrome.
- Chemins directs listés plus haut et champs d'édition GDI : non corrigés.
- Coût : chaînes ordinaires inchangées (un balayage) ; une chaîne à emoji paie une fois (cache Chrome) une recherche de police et un passage de mise en page ; PopupKit, sans cache, le paie à chaque dessin de ces seules chaînes ; couche seulement si couleur réelle et alpha < 1.
- Pas d'AGENTS.md dans le dépôt à cette base ; seules les compétences demandées ont été suivies.
