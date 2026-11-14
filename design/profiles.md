# Profiles — values exported from Paper (p-I-0, read 2026-11-14, tokens hash b37eaa4f)

Source of truth: Paper file « Browser Design », page « Profils — flow ». Read-only export (get_jsx / tokens); no Paper mutation.

## Tokens
canvas #171717 · raised #292929 · hover #333333 · pressed #3E3E3E · text #FFFFFF · danger #FF7B6E · reference accent #3E67FF
Profile inks: gris #B3B3B3 · vert #6FD087 · cyan #4FC3D9 · violet #B49BFF · rose #F08BC4 · ambre #E6AC3D
Radii 4 / 8 / 12 / 16 (plus legacy 3 / 6). Opacity disabled 32 %, secondary 50 %, focus 10 %.

## Profile button (tab-strip, replaces the flower)
Slot: strip padding-left 6, padding-top 6, bottom 2 → plate 32×32 at (6,6)… glyph 16 Fill, centre (22,22); first tab follows at gap 6.
Plate = profile ink at 10 % rest · 16 % hover (120 ms) · 22 % pressed/menu open, scale .96 · focus 10 % + 1 px #FFFFFF80 ring. Radius 8.
Private window: flower + « Privée » chip unchanged, reference accent.

## Profile menu (248 wide)
Surface canvas, border 1 #FFFFFF0F, radius 12, padding 4, shadow 0 8 24 #00000066.
Rows h32, radius 8, padding-inline 8, gap 8. Text 14/20 Regular #FFFFFFE6 (hovered row #FFFFFF).
1. « Liste des onglets (n)… » — glyph copy (bold) 16 #FFFFFFB3.
2. Separator h9, line 1 px #FFFFFF0F inset 8.
3. Profile rows: lanes glyph 16 (Fill, ink) / name flex ellipsis / windows 36 (browser bold 14 #FFFFFFB3 + gap 4 + 13/16 Medium tnum #FFFFFF99, right aligned, empty if 0) / check 16 (bold check 14 #FFFFFFBF, current profile only).
   Hover/focus row: ink at 10 %.
4. Separator. 5. « Nouveau profil… » (plus bold). 6. « Gérer les profils… » (gear bold).
UIA row name: « Travail, cette fenêtre, 1 fenêtre ouverte ».

## Tab preview (288 wide)
Same surface as menu; rows h28 radius 8 padding 8 gap 8; favicon 16 radius 4 (assets/sites/<host>.png, else initial tile); title 14/18 Regular #FFFFFFE6 one line ellipsis.
Opens 400 ms after hover on a row, beside it; closes 300 ms after leaving; safe corridor; grouped per window; max 6 rows then « + n autres onglets »; no preview when the profile has no window. Private windows never shown.

## Keyboard (menu)
Button first stop (profile → tabs). Entrée/Espace/↓ open; ↑/↓, Début/Fin; Entrée activates. → enters the preview (↑/↓, Entrée opens that tab in its window); ← or Échap back; second Échap closes. Ctrl+Maj+M. Wheel over the button switches tabs.
Tooltip keycaps: h20, min-w 24, padding 6, #FFFFFF14, radius 4, 13/16 Medium #FFFFFFCC, gap 4.

## Settings › Profils
Root row « Profils » (user-circle bold 16) value = current profile glyph (16, ink) + name 14 #FFFFFF80 + chevron.
List sub-view: caption « 3 profils » 14/20 Medium #FFFFFF80 (pad-left 8, bottom 4); rows h40 gap 2, radius 8, pad 8/4, glyph 16 ink, name 16/20 −0.01em (#FFFFFF current/hover, else #FFFFFFB3), check lane 16, chevron lane 24. Hover/current tint: ink 10 %.
Detail sub-view (title = profile name, back + close 24): content pad-inline 8, gap 20:
- « Nom » label 14/20 Medium #FFFFFF80; field h32 #FFFFFF14 radius 8 pad 10, text 16/20; help « 32 caractères au plus. Enregistré avec Entrée ou en quittant le champ. » 14/18 #FFFFFF80.
- « Icône » 14/20 #FFFFFFB3; grid 6×2 of 32×32 cells gap 4 radius 8, glyph 16 #FFFFFFB3; selected = ink 16 % + inset 1.5 px #FFFFFF (+ 2 px #FFFFFF80 outer when focused), glyph in ink.
- Colour block (see below).
- Footer (border-top 1 #FFFFFF0A, pt 12): note « Supprime son historique, ses cookies, ses mots de passe et ses favoris de cet appareil. » 14/18 #FFFFFF80; « Supprimer le profil… » 16/20 danger, h32.
- Last profile footer: note « Il faut au moins un profil. Pour repartir de zéro : » + row « Effacer les données… » (chevron).
Rules: name trimmed, unique case/accent-insensitive, ≤32 (input blocked beyond). Entrée on invalid: error under field, focus kept; blur: revert to last valid silently. Announce « Nom enregistré ».

## Colour block (shared by modal + detail)
Two rows h24 gap 8: track (flex) + value 36 (13/16 Medium tnum, #FFFFFF80, centred).
Track 12 high radius 6 at y 6. Hue gradient = OKLCH L .78 C .14 around the wheel; intensity gradient #B7B7B7 → oklch(.78 .16 h).
Thumb 4×20 radius 2 white, 1 px #00000052 edge, at y 2, centred on value. Drag 6×24 + shadow; focus ring +3 px #FFFFFF80.
Keys ← → 1, Maj 10, Page 30, Début/Fin; hue wraps on keyboard. Announce « Teinte 200°, cyan ».
Preview row (pt 4, gap 8): plate 32 ink 10 % + glyph; « Aa » chip 40×32 accent fill, 14/18 Medium white; hex field 96×32 #FFFFFF14 radius 8 pad 10, 14/20 tnum.
Hex entry keeps the hue, L clamped to .78.

## New-profile modal (400, A1)
canvas, border 1 #FFFFFF14, radius 16, padding 16, gap 16.
Title « Nouveau profil » 16/20 Medium −0.01em; body « Historique, cookies, mots de passe et favoris séparés, sur cet appareil. Il s'ouvre dans une nouvelle fenêtre. » 14/20 #FFFFFFB3.
Fields: Nom (label 14/20 #FFFFFFB3, gap 8, help « 32 caractères au plus. »), Icône, Couleur.
Actions (pt 8, gap 8, end): « Annuler » raised; « Créer le profil » accent fill of the window; min-w 64, h32, pad 12, 14/18 Medium. Duplicate checked on submit.
Result: modal fades (120 ms), the new profile opens in a new window.

## Delete (400, A1 + destructive)
Title row: glyph 16 ink + « Supprimer le profil « X » ? » 16/20 Medium.
Block (border top/bottom 1 #FFFFFF0D, py 12, gap 12): « Effacé de cet appareil » / « Historique, cookies et données des sites, mots de passe, favoris, autorisations et réglages de ce profil. »; « Conservé » / « Les fichiers téléchargés restent sur le disque. Les autres profils ne changent pas. » (14/20 text, 14/18 #FFFFFF80).
« Vous pourrez annuler pendant 10 secondes. » 14/20 #FFFFFFB3.
Buttons: « Annuler » raised (focused), « Supprimer le profil » plate #FF7B6E24 text danger.
In progress: controls locked, Échap inert. Failure: warning-circle danger + « Impossible de supprimer ce profil : un de ses fichiers est utilisé par un autre programme. Rien n'a été effacé. », button « Réessayer » focused.
Toast 320, radius 16, pad 16, gap 16: check-circle .5 + « Profil « X » supprimé »; « Effacement définitif dans 7 s. » tnum + « Annuler ». Countdown announced once, pauses on focus/hover.
Undo toast: « Profil « X » rétabli » / « Ses données et les onglets de ses n fenêtres sont intacts ; elles ne se rouvrent pas seules. » + « Rouvrir ses fenêtres » (only if windows were closed).

## Per-window accent
Fill = OKLCH C .14, L solved per hue for white ≥ 4.6:1 (0° #BE4D77, 25° #BD524D, 55° #B85900, 120° #677D00, 150° #1A8641, 200° #00818C, 240° #0079BF, 300° #8760C3; neutral #747474). Ink = profile colour (L .78). Selection tint = ink 10 %.
Danger/error/warning unchanged; focus ring white. Colour edits update open windows live (120 ms fade).

## Motion
hover 120 ms · press 150 ms (.96) · menu 180 in / 150 out · settings sub-view spring .34 · preview 150 ms · accent fade 120 ms · icon swap 300 ms cubic-bezier(.2,0,0,1). Reduced motion: opacity only or instant.

## Changes noticed vs the previous summary (latest user edits)
- Colour rows no longer have the 64 px label lane: track + 36 value only.
- Detail view « Fenêtres » row is now an empty 32 px placeholder (no « Ouvrir une fenêtre » row drawn).
- Settings list rows are 40 high with a 24 chevron lane.
