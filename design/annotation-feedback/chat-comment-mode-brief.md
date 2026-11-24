# Chat — Mode Commentaire (brief commun, 18 novembre 2026)

Nouveau mode conçu par l'utilisateur dans Paper, **en plus** du chat en panneau à droite. Fichier Paper `01M6Q7YT14ZKMGET54ASPNGQEC`, page « MCP - Design UI UX » (`p-H-0`), rangée y ≈ 13 659 / 14 547. Lis les planches avec le MCP Paper (`get_tree_summary`, `get_jsx`, `get_computed_styles`, `get_screenshot`). Ne lis jamais une valeur sur une capture.

| Planche | Node | Contenu |
|---|---|---|
| 1 · élément visé + Commentaire | `18NS-0` | clic sur un élément → la bulle de saisie s'affiche à côté, dans l'espace libre |
| 2 · Prompt envoyé | `1B30-0` | la conversation reste dans la bulle : message, modèle · effort, étape en cours (« Lit checkout.css… »), texte de l'agent, bouton arrêter |
| 2.1 · Prompt envoyé, Hover details | `1AIE-0` | survol de l'étape en cours → infobulle avec la liste des étapes (A lu index.html, A lu Checkout.tsx, A utilisé 1 outil MCP) |
| 2.1 · Prompt stoppé | `1AE7-0` | arrêt : « S'est arrêté » + icône, la saisie revient (« Que voulez-vous ajouter ? ») |
| 2 · Prompt envoyé (autorisation) | `1ANU-0` | la demande d'autorisation est dans la bulle : « Claude veut modifier checkout.css +6 −2 › », « Dire à Claude quoi faire à la place… », interrupteur Auto, Refuser / Autoriser ; le haut du fil s'estompe |
| 2 · Prompt envoyé (diff ouvert) | `19ON-0` | clic sur la ligne de fichier → le diff s'ouvre en grand dans la bulle (titre, +6 −2, fermer) au-dessus des boutons d'autorisation |
| 2.2b · Prompt Erreur | `1AAE-0` | erreur : texte de l'agent en rouge, « Une erreur est apparue. » + « Plus d'infos › », Annuler / Réessayer |
| 2.2b · Prompt Erreur (détails) | `1A6L-0` | « Plus d'infos » déplié : code, explication en clair, ce qui a été changé ou non |
| 3 · Prompt terminé | `1AWI-0` | « C'est fait… », barre « 1 fichier modifié +6 −2 › » + Copier, « Historique » à droite du modèle, saisie « Que voulez-vous ajouter de plus ? » ; **bandeau : sélecteur Commentaire / Panneau** à gauche de Quitter (`1AYP-0`) |
| 3 · Prompt terminé (code ouvert) | `19IO-0` | le diff du fichier ouvert en grand dans la bulle avec Copier |
| 4 · Prompt terminé - Désélectionné | `18IJ-0` | après avoir quitté la bulle : bordure + icône en haut à gauche restent sur l'élément |

Commentaire de l'utilisateur sur `18KS-0` (planche 4) :
> Quand le prompt est fini et qu'on se retire de la conversation (clic ailleurs et/ou Échap), la bordure et l'icône restent pour indiquer qu'une conversation a déjà été faite. Quand on reclique dedans, on retrouve la conversation, ce qui sert d'historique.
> Attention : quand l'ancienne frame disparaît et/ou bouge, déplacer la conversation aussi pour qu'elle ne disparaisse pas. Si la frame disparaît et que la conversation n'est plus disponible à travers une frame, la mettre dans l'historique en bas à droite, qui servira uniquement aux conversations disparues.

## Règles du mode
1. **Deux modes**, choisis par le sélecteur du bandeau (`1AYP-0`, deux icônes : bulle = Commentaire, panneau = Chat à droite). Le dernier choix est mémorisé. Le mode Chat à droite reste celui d'aujourd'hui (V1.4).
2. **Commentaire** : un clic sur un élément ouvre une **bulle compacte** (≈ 305 px de large, voir les planches) placée dans l'espace libre à côté de l'élément (à droite si possible, sinon à gauche, dessous, dessus), jamais hors de la fenêtre. Elle reste collée à l'élément au défilement, sans retard.
3. **Tout ce qui concerne cet élément reste dans la bulle** : saisie, fil, étapes (infobulle au survol), arrêt, autorisations, diff en grand, erreurs, résultat, Copier, Historique des actions (infobulle au survol de « Historique »). Pas d'en-tête, pas de liste de conversations : c'est compact.
4. **Une conversation par élément.** Échap ou clic ailleurs ferme la bulle sans arrêter l'agent. L'élément garde la bordure + l'icône (planche 4), y compris pendant que l'agent travaille. Un clic dessus rouvre la bulle avec la conversation.
5. L'élément **bouge** (défilement, mise en page, rechargement après modification) → bordure, icône et bulle le suivent. On le retrouve après rechargement par son sélecteur / sa source (fichier:ligne), comme le cadre « fait » d'aujourd'hui.
6. L'élément **disparaît** (introuvable après rechargement ou changement de page) → la conversation part dans l'**historique en bas à droite** (le lanceur existant), qui ne sert plus qu'aux conversations dont l'élément a disparu. Depuis l'historique, on peut la rouvrir (dans la bulle, ancrée au bas de la fenêtre, ou dans le panneau).
7. Même moteur que le chat : sessions ACP, autorisations, copies et annulation, magasin d'annotations (claim, resolve, release), sites locaux seulement, Ctrl+Entrée = garder en annotation. Les noms de modes restent en **français** (Demander, Auto, Planifier) même si la maquette montre « Ask ».
8. Passer d'un mode à l'autre ne perd rien : une conversation de bulle peut s'ouvrir dans le panneau (et inversement, si elle est liée à un élément).
9. Retour en plus (commentaire Paper sur `156L-0`) : là où un dossier de projet est montré, afficher **le nom du dossier** plutôt que le chemin ou le lien.

## Qualité
Skills obligatoires : `<Bureau>/skills/` (apple-design, emil-design-eng, better-ui, better-interface, better-layout, better-typography, better-accessibility, better-writing). Mouvement : ouverture de la bulle depuis l'élément (origine côté élément), fermeture plus rapide que l'ouverture, `prefers-reduced-motion` respecté, aucun flash. Accessibilité : la bulle est une boîte de dialogue non modale, focus dans la saisie à l'ouverture, Échap la ferme, lecteur d'écran.

## Règles de sécurité (inchangées)
Jamais de modification des vraies configs Claude/Codex (`%APPDATA%/Claude`, `%USERPROFILE%/.codex`, `~/.claude`). Tests avec APPDATA/USERPROFILE/CODEX_HOME/PATH redirigés et `BROWSER_ANNOTATIONS_TEST=1`, faux agent uniquement. Une seule suite en vraie fenêtre à la fois, rien laissé ouvert, pas d'automatisation graphique d'autres applications, profils de navigation intacts. Scripts écrits avec l'outil Write. Pas de push, pas de fusion.
