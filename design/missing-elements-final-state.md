# « Éléments & popups » — état final après les retours utilisateur (2026-11-09)

Synthèse des correctifs validés en preview (les rapports détaillés de QA étaient dans `build/`, non suivi).
Référence Paper : fichier « Browser Design », page p-D-0 (B01–B08, C5), artboard ajouté « B08b · Partage d'écran — sélecteur avec aperçus ».

## Autorisations (B01/B02)
- Puce de barre d'adresse (`src/site-request.h`, `chrome.h`) : en attente pendant le popover (#3E3E3E), puis
  « autorisé » (glyphes 70 %, réponse réelle + réglages mémorisés du moteur), « bloqué » (barre oblique),
  « en cours » (teinte + point bleu) **seulement en démonstration** : WebView2 n'expose aucun état d'usage
  caméra/micro (`ScreenCaptureStarting` = écran, `IsDocumentPlayingAudio` = sortie audio).
- Portée onglet + document top-level ; nettoyage à la navigation, au changement/fermeture d'onglet, à l'arrêt.
- Clic : retour au popover ouvert ou popover « état du site » (bascules à sens unique ; retrait = réglage
  « Demander » + rechargement), lien vers Réglages › Autorisations (ouvert directement sur cette vue).
- Une demande arrivée pendant l'édition de l'adresse attend la fin de l'édition.

## Popovers (règle B06 généralisée)
- Titres 16/20 Medium partout (Basic auth, certificat, partage : 20/24 → 16/20 ; override utilisateur de Paper).
- Colonne d'en-tête (`Modal::Dialog::SecondaryLane`) : tuile 32 → texte 44, glyphe 24 → texte 32, lignes
  groupées 4 + 24 → texte 36. Case de mémorisation et notices centrées dans cette colonne, texte sur la
  colonne de la description, 14/18 regular, 70 % si interactif (60 % pour la description).
- Ligne d'émetteur du certificat : sans le tracking 0.08em des points de mot de passe.
- B07 « Plus d'options » : menu natif ancré, « Conserver quand même… » → confirmation dans le popover
  (Annuler par défaut). Démonstration ; le vrai flux dangereux reste celui du moteur.
- B08 partage d'écran : fenêtre 640, catégories + vignettes fictives dessinées localement, « Partager » explicite.

## Focus visible
`Modal::Spec::modality` (Host / Pointer / Keyboard) : le focus réel va toujours sur le contrôle initial sûr ;
l'anneau n'apparaît qu'après une entrée clavier (ou une ouverture au clavier / UIA). Les demandes venant
d'une page sont « Pointer ». Le tiroir Réglages et les menus (« Ouvrir tout ») transmettent la modalité réelle
de leur activation (`ContextMenu::Choice::keyboard`).

## Divers
- Tiroir Réglages : `Show(View)` ouvre directement sur la vue cible (plus de page racine sous l'entrée) ;
  fondus de défilement opaques au bord, hauteur selon le défilement restant, bords alignés sur les pixels.
- C5 info-bulles et panneau des raccourcis : fond canvas #171717.
- Pages d'erreur réseau : actions sur une ligne, « Réessayer » à droite, empilement sous 360 px.
- Mouvement : puce 150/120 ms, changement d'état 120 ms ; popovers : sortie qui refait les 4 DIP d'entrée ;
  mouvement réduit = fondus ; aucune horloge au repos ; zéro ombre.

## Limites connues
- Pas d'état « en cours » réel (API absente).
- Dialogue JS déclenché au clavier dans une page : pas d'anneau tant que Tab n'est pas pressé.
- Retrait d'une autorisation : seule la dernière origine demandeuse d'une page est retenue.
