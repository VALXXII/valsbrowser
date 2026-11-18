# Tests du panneau de chat

```
python -I tests/chat-panel/run.py                 # tout
python -I tests/chat-panel/run.py --only 6.1-autorisation,6.12-plan
python -I tests/chat-panel/run.py --edge <msedge.exe> --out <dossier>
```

- Moteur : le Chromium du runtime WebView2 (`%LOCALAPPDATA%\Microsoft\EdgeCore\<version>\msedge.exe`), sans tête, profil jetable, piloté par DevTools. Fermé dans un `finally`.
- Pages servies par un petit serveur HTTP sur 127.0.0.1 (port libre) : les polices ne se chargent pas en `file://`. Aucun accès réseau, aucun agent réel, `BROWSER_ANNOTATIONS_TEST=1`.
- `assets/chat-panel/dev.html?scenario=<nom>` rejoue un scénario du faux hôte (`dev-host.js`). Sans `scenario`, le faux hôte répond aux envois (mode interactif) : `python -m http.server -d assets 8000` puis `http://127.0.0.1:8000/chat-panel/dev.html`.

- Bulle du mode Commentaire (contrat B 1.8) : scénarios `c-…` (`--only "c-*"` les lance seuls ; une étoile finale = préfixe). Le faux hôte joue la fenêtre : il prend la taille demandée par `bubble.size`. Essai à la main : `http://127.0.0.1:8000/chat-panel/dev.html?layout=bubble`.

## Ce qui est vérifié

1. Statique : pas d'`eval`, `new Function`, `fetch`, XHR, WebSocket, stockage, import dynamique ; `innerHTML` seulement pour les icônes constantes ; CSP stricte ; aucune ressource distante ; `index.html` ne charge pas le faux hôte.
2. Chaque scénario : rejoué sans erreur, sans violation CSP, sans `panel.log` ; audit d'accessibilité (`a11y.js` : noms accessibles, tabindex, ids, aria-*, régions vivantes) ; capture dans `build/chat-qa/panel/<scénario>.png`.
3. Comparaison avec les planches Paper (`reference/`, exportées des planches 6.1 à 6.13) : `build/chat-qa/panel/compare/<scénario>.png` (planche | panneau | carte des écarts quand les tailles sont égales), écart moyen dans la sortie. Planches-contact : `build/chat-qa/panel/planches-N.png`.
4. Clavier : carte d'autorisation (focus, Entrée, Échap), Échap pendant un tour (stop) ou à vide (fermer), Ctrl+/ et flèches dans les menus, Maj+Tab (mode), menu @ (aria-activedescendant, Entrée), envoi par Entrée, ordre de Tab, Échap dans l'historique, confirmation avant de renvoyer un message modifié.
5. Comportements : bouton « nouvelles étapes », Markdown sûr (HTML brut affiché comme texte, code > 15 lignes replié, liens confiés à l'hôte), mouvement réduit, page seule sans requête hors de ses fichiers, polices chargées.
6. Performance : fil de 500 messages (rendu et défilement image par image), texte en flux à ~60 morceaux/s pendant 3 s (images, tâches longues).
7. Bulle du mode Commentaire : chaque état des planches Paper `18NS-0` à `19IO-0` (saisie, tour en cours, survol des étapes, arrêté, autorisation, diff en grand, erreur et ses détails, terminé, diff du résultat), capturé à la taille que la bulle demande, sur fond transparent, dans `build/chat-qa/comment-v2/panel/<scénario>.png` (la surface seule) et `<scénario>-fenetre.png` (toute la fenêtre accordée, avec ce qui flotte hors de la surface) ; comparé à la bulle de sa planche (`reference/comment/`, exportée de Paper à 1x) : même taille au pixel, puis carte des écarts dans `build/chat-qa/comment-v2/panel/compare/`. Comportements : `layout` / `bubble.size` (hauteur de chaque planche, accusé de réception d'une ouverture seulement, bornes), Échap = `close` sans `stop` ni refus, focus, menus et infobulles hors de la bulle (`overflow*` demandés, `layout.inset` accordé, marge refusée au bord de l'écran : le menu se retourne), les 4 modes de Paper, « + » sans dossier sûr, pas de puce sous le message, l'étape en cours à la place du message, logo du fournisseur, carte d'autorisation renvoyée après `thread.reset`, file d'attente entre éléments, fil long (défilement et fondu), tour en direct, mouvement réduit, retour au panneau.
8. Nom du dossier à la place du chemin ou de l'adresse (écran du dossier, historique ; « + » ne montre plus un dossier sûr).
9. Fidélité valeur par valeur (révision 1.9) : `fidelity.json` relie chaque nœud du relevé Paper (`design/chat-comment-paper-values.md`) à son élément ; chaque boîte, rayon, marge, écart, fond, police et couleur est mesuré dans la page et comparé ; tableau `build/chat-qa/comment-v2/panel/ecarts.md`, zéro écart non justifié exigé (les justifications sont dans `run.py`, `JUSTIFIED`, et au §20.9 du contrat).
10. En-tête du panneau : le titre n'ouvre plus l'historique ; double-clic ou F2 = renommer en place (`conversation.rename`, Entrée / Échap).
