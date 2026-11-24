# Mode Commentaire + Chat — version finale avant publication (21 novembre 2026)

Exigence de l'utilisateur : **la prochaine version qu'il teste est la dernière avant le public. Zéro bug, tout poli, exactement Paper.** Personne ne rend « fini » sans preuve fraîche (skill `verification-before-completion`).

## Sources de vérité
- Retours et décisions : `design/chat-comment-v2-retours.md` (R1 à R48, D1 à D8) et `design/chat-comment-mode-brief.md`.
- Design : Paper `01M6Q7YT14ZKMGET54ASPNGQEC` + `design/chat-comment-paper-values.md` (worktree) ; décisions de l'utilisateur prioritaires là où les retours le disent.
- Contrat page ↔ hôte : `design/chat-panel.md` (contrat B, §1 à §25).
- Skills : `<Bureau>/skills/` (apple-design, emil-design-eng, better-*, interface-review, break, systematic-debugging, verification-before-completion).

## Phases
1. **Corrections en cours** : H1 (R44 placement + première ouverture d'une conversation existante, R46, R47 hôte), P (R47 page, R48), H2 (R45).
2. **Revue indépendante** (agent R, lecture seule) : machine d'états de la bulle (ouverte / masquée / fermée / placement / conversation / file), défilement, région, rechargements, bascules de mode, perte du premier plan, sessions ACP (démarrage, modes, changement de modèle). Rapport des défauts probables, avec scénario de reproduction. Chaque défaut va à son propriétaire.
3. **Parcours réels et test de désordre** (agent Q) : (a) un test par séquence signalée par l'utilisateur (toutes les R et tous les bugs, dans l'ordre exact où il les a vécus) ; (b) un test « monkey » en vraie fenêtre : 300+ actions aléatoires reproductibles (graine) — clic sur élément avec/sans conversation, clic ailleurs, Échap, défilement molette/barre/clavier, bascule Commentaire/Chat, autre application au premier plan puis retour, coller du texte, ouvrir/fermer menus, diff, poignée, Ctrl maintenu, envoi d'un message, autorisation, arrêt — avec des **invariants vérifiés après chaque action** (liste ci-dessous) ; (c) le faux agent rejoue des sessions réalistes (démarrage en mode auto, annonces puis réponse, adresses nues, Markdown long, blocs de code, autorisations, erreurs, surcharge, réponse en français).
4. **Audit de fidélité final** (agent F) sur les captures de la phase 3.
5. **Rien ne part à l'utilisateur** tant que : 0 échec sur toutes les suites (panneau, unitaires, ACP, copies, annotations-ui, i18n, smoke, toutes les vraies fenêtres v2 à v5b, parcours réels, monkey 3 graines), 0 écart Paper non justifié, build 0 avertissement, R1 à R48 fermés avec preuve.

## Invariants du test de désordre (après chaque action)
- Un clic sur un élément en mode Commentaire montre une bulle **entièrement visible** (dans la zone de page), à sa hauteur naturelle, en moins de 150 ms, avec la bonne conversation.
- Une bulle ouverte est toujours collée à son élément (±2 px hors molette), jamais coupée par la région, jamais vide, jamais grisée hors du haut du fil.
- Fenêtre de bulle visible ⇔ état « ouverte » ; aucune bulle fantôme ni état ouvert invisible.
- Le mode, le modèle et l'effort affichés ne changent que par une action de l'utilisateur.
- Les étiquettes du cadre sont identiques au survol et à la sélection ; un seul contour par élément ; la marque « conversation » n'existe qu'après un envoi.
- Aucun message d'erreur JavaScript dans la page du chat, aucune exception dans le journal de l'hôte, aucun processus ou fenêtre orphelin à la fin.
- Accessibilité : l'arbre UIA de la fenêtre principale reste complet (le chat est lisible).

## Sécurité (inchangé)
Jamais les vraies configs Claude/Codex ; tests avec APPDATA/USERPROFILE/CODEX_HOME/PATH redirigés, `BROWSER_ANNOTATIONS_TEST=1`, faux agent. Une seule suite en vraie fenêtre à la fois (Q possède la vraie fenêtre en phase 3). Profils de navigation intacts. Pas de push ni de fusion.
