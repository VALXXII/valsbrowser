# Tests du panneau de chat

```
python -I tests/chat-panel/run.py                 # tout
python -I tests/chat-panel/run.py --only 6.1-autorisation,6.12-plan
python -I tests/chat-panel/run.py --edge <msedge.exe> --out <dossier>
```

- Moteur : le Chromium du runtime WebView2 (`%LOCALAPPDATA%\Microsoft\EdgeCore\<version>\msedge.exe`), sans tête, profil jetable, piloté par DevTools. Fermé dans un `finally`.
- Pages servies par un petit serveur HTTP sur 127.0.0.1 (port libre) : les polices ne se chargent pas en `file://`. Aucun accès réseau, aucun agent réel, `BROWSER_ANNOTATIONS_TEST=1`.
- `assets/chat-panel/dev.html?scenario=<nom>` rejoue un scénario du faux hôte (`dev-host.js`). Sans `scenario`, le faux hôte répond aux envois (mode interactif) : `python -m http.server -d assets 8000` puis `http://127.0.0.1:8000/chat-panel/dev.html`.

## Ce qui est vérifié

1. Statique : pas d'`eval`, `new Function`, `fetch`, XHR, WebSocket, stockage, import dynamique ; `innerHTML` seulement pour les icônes constantes ; CSP stricte ; aucune ressource distante ; `index.html` ne charge pas le faux hôte.
2. Chaque scénario : rejoué sans erreur, sans violation CSP, sans `panel.log` ; audit d'accessibilité (`a11y.js` : noms accessibles, tabindex, ids, aria-*, régions vivantes) ; capture dans `build/chat-qa/panel/<scénario>.png`.
3. Comparaison avec les planches Paper (`reference/`, exportées des planches 6.1 à 6.13) : `build/chat-qa/panel/compare/<scénario>.png` (planche | panneau | carte des écarts quand les tailles sont égales), écart moyen dans la sortie. Planches-contact : `build/chat-qa/panel/planches-N.png`.
4. Clavier : carte d'autorisation (focus, Entrée, Échap), Échap pendant un tour (stop) ou à vide (fermer), Ctrl+/ et flèches dans les menus, Maj+Tab (mode), menu @ (aria-activedescendant, Entrée), envoi par Entrée, ordre de Tab, Échap dans l'historique, confirmation avant de renvoyer un message modifié.
5. Comportements : bouton « nouvelles étapes », Markdown sûr (HTML brut affiché comme texte, code > 15 lignes replié, liens confiés à l'hôte), mouvement réduit, page seule sans requête hors de ses fichiers, polices chargées.
6. Performance : fil de 500 messages (rendu et défilement image par image), texte en flux à ~60 morceaux/s pendant 3 s (images, tâches longues).
