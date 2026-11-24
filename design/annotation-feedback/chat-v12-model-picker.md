# Chat V1.2 — point 8 : nouveau menu modèle · effort (design Paper mis à jour)

Référence Paper : fichier `01M6Q7YT14ZKMGET54ASPNGQEC`, artboard « Choix du modèle, de l'effort et du mode » (node `14P2-0`). Menus : `17US-0` (modèle), `17VU-0` (effort), `14RK-0` (mode), bas de saisie `14P9-0`. Lis-les avec le MCP Paper (`get_jsx`, `get_computed_styles`) pour les valeurs exactes. Ne lis jamais les valeurs sur une capture.

Cette décision **remplace** celle de la V1.1 (« un seul popover, sans Codex ») et précise le point 2 de la V1.2.

## Menu modèle (s'ouvre vers le haut, 300 px)
- Un seul menu pour **tous les agents connectés**. Un groupe par fournisseur, séparé par un trait (1 px `#FFFFFF14`, marges 4 px / 10 px) :
  - groupe Claude Code : ses modèles (dans le design : Opus 5.5, Sonnet 5.5, Haiku 5.5) ;
  - groupe Codex : ses modèles (dans le design : « 6.1 Sol », « 6.1 Terra », « 6.1 Luna », des noms d'exemple).
- Les noms viennent de ce que l'agent annonce (listes vivantes, jamais figées). Le modèle par défaut de l'agent porte le badge « Par défaut » (fond `#FFFFFF14`, rayon 4, 10 px / 500, `#FFFFFFB3`).
- Ligne choisie : fond `#FFFFFF0F` et coche bleue `#8FA9FF` à droite. Lignes : padding 6/10, rayon 8, texte 13/18, poids 500.
- **Afficher un fournisseur seulement s'il est connecté.** Si Claude Code ou Codex n'est pas installé ou pas connecté, son groupe et son séparateur disparaissent complètement (aucune ligne grisée, aucun « Se connecter » dans ce menu). Un seul fournisseur connecté = un seul groupe, sans séparateur de groupe. Mets la liste à jour quand l'état de connexion change.
- Choisir un modèle de l'autre fournisseur change d'agent : nouvelle conversation, avec l'avertissement déjà prévu (« Passer à Codex ouvre un nouveau chat. Celui-ci reste dans l'historique. »). Changer de modèle chez le même agent s'applique au message suivant.
- Puis un séparateur, et deux lignes à sous-menu (chevron 11 px, opacité 0.5, tourné de -90°) :
  - « Autres modèles › » : les modèles en plus si un fournisseur en annonce plus que ceux montrés (au-delà de 3 par groupe), sinon la ligne est masquée ;
  - « Effort  <valeur> › » : la valeur actuelle à droite en `#FFFFFF99` 12/16.

## Sous-menu effort (214 px)
- Niveaux affichés en français : **Faible, Moyen, Élevé, X-Élevé, Max**, d'après ce que l'agent annonce (low / medium / high / xhigh / max) ; n'affiche que les niveaux que le modèle choisi accepte.
- Pas de « Default ». Valeur par défaut : **Moyen** (voir point 2).

## Bas de la zone de saisie
- À gauche : viser, +, puis la puce de mode (fond `var(--color-browser-split-tint)`, icône + texte bleu `#8FA9FF`, hauteur 24, rayon 6).
- À droite : le bouton modèle avec le **logo du fournisseur** (14 px), le nom du modèle (12/16, 500, `#FFFFFFE6`), puis l'effort (`#FFFFFF80`) et un chevron vers le bas (10 px, opacité 0.5), puis envoyer (28×28, accent, rayon 8).
- Si la place manque, le nom du modèle se coupe en premier, jamais l'effort.
- En-tête des réponses : « Opus · Moyen · 4 s » (plus jamais « Default »).

## Clavier
Ctrl+/ ouvre le menu modèle, flèches et Entrée dans les menus, flèche droite ouvre un sous-menu, flèche gauche ou Échap le referme. Maj+Tab change de mode.

## Preuve
Tests du panneau : un seul fournisseur connecté, puis les deux, puis aucun (le bouton modèle affiche alors l'état de connexion existant) ; changement d'agent depuis le menu ; effort en français sans « Default ». Captures dans `build/chat-qa/v12/` à comparer aux menus Paper `17US-0` et `17VU-0`.
