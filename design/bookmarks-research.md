# Favoris — recherche Dia et Arc

Date de collecte : **8 novembre 2026**. Périmètre : recherche UX seulement. Aucune modification du code de l'application ni de Paper.

Lecture locale préalable : pas d'`AGENTS.md` dans ce worktree ; [design-system.md](design-system.md) et le `README.md` ont été lus pour adapter la recommandation à la barre horizontale existante.

Légende utilisée plus bas :

- **Vérifié** : écrit tel quel dans une source primaire (centre d'aide Arc, changelog officiel Dia, aide Chrome), avec la date de la source.
- **Inférence** : déduction de ma part à partir de ces sources ou de captures. Aucun des deux navigateurs n'a été installé ni testé ici.

## 1. Contexte à connaître avant de copier Arc

- **Vérifié — Arc s'arrête le 22 décembre 2026.** Il est impossible de créer un compte Arc depuis le 22 octobre 2026. Dia, de la même équipe, remplace Arc. [Arc for Desktop](https://resources.arc.net/hc/en-us/articles/19337669022103-Arc-for-Desktop-macOS-and-Windows) (mis à jour le 2026-10-22) ; [Arc Sunset FAQ](https://resources.arc.net/hc/en-us/articles/44413577819159-Arc-Sunset-Frequently-Asked-Questions) (2026-10-22).
- **Conséquence :** Arc sert ici de référence historique du modèle « onglets épinglés = favoris ». Le comportement **actuel** à suivre est celui de Dia, en particulier Dia pour Windows : version 1.0 environ, puis 1.1.0 à 1.5.0 entre le 8 octobre et le 5 novembre 2026.
- **Vérifié — l'ancien article Arc « Where are Bookmarks » n'est plus servi.** URL : `https://resources.arc.net/hc/en-us/articles/19400407903767-Where-are-Bookmarks`. Il renvoie 403 sur la page web et 404 via l'API Zendesk le 2026-11-08. Son extrait reste indexé par les moteurs : « Arc for desktop doesn't have bookmarks – we used Pinned Tabs instead. Just like bookmarks, Pinned Tabs can be stored in Folders… Command-T… "New Folder" ». Comme la page n'a pas pu être relue, cet extrait est classé comme historique.

## 2. Arc — ce qui est vérifié

Sources :

- [Arc Sunset FAQ](https://resources.arc.net/hc/en-us/articles/44413577819159-Arc-Sunset-Frequently-Asked-Questions) (2026-10-22)
- [Move from Arc to Dia](https://resources.arc.net/hc/en-us/articles/44413129117591-Move-from-Arc-to-Dia) (2026-10-22)
- [How Do I Export My Data from Arc?](https://resources.arc.net/hc/en-us/articles/25583851606039-How-Do-I-Export-My-Data-from-Arc) (2026-10-29)
- [Arc for Windows Release Notes](https://resources.arc.net/hc/en-us/articles/22513842649623-Arc-for-Windows-2023-2026-Release-Notes) (2026-11-05)

| Sujet | Comportement Arc (vérifié) |
|---|---|
| Modèle | Arc n'a pas de barre de favoris classique. Trois niveaux existent : **Favorites** (icônes partagées par tous les Spaces), **Pinned Tabs** (propres à un Space, avec leur titre) et **Folders**. Les sources les plus récentes le confirment indirectement : la FAQ de sortie d'Arc décrit ce qui part vers Dia, et l'export « liste vos Favorites, pinned tabs, folders and other sidebar tabs ». |
| Dossiers | Les dossiers peuvent être imbriqués. La FAQ précise que les « folders inside other folders » deviennent des groupes séparés dans Dia, ce qui confirme l'imbrication côté Arc. Clic droit sur un dossier : **Copy All Links**, et sous macOS Option pour obtenir **Copy All Links as Markdown**. |
| Retrouver | Il n'y a pas de gestionnaire de favoris. On retrouve un site par la barre latérale ou la Command Bar (`Ctrl+T`), qui sert aussi aux commandes (« unfavorite tab », « New Folder »). La Command Bar s'ouvre sur l'URL courante avec `Ctrl+L` ou `Alt+D` (notes Windows). |
| Export | Le fichier `Arc Data Export.md` est une liste de liens, pas un fichier de favoris importable : « you'll add the links to your new browser yourself ». |
| Comportement vivant | Les Live Folders GitHub se remplissent seuls de pull requests. Une icône Google Calendar épinglée en favori affiche un compte à rebours (notes Windows). |

**Inférence :** chez Arc, épingler, sauvegarder et garder l'onglet ouvert ne font qu'un. Ce modèle demande un panneau latéral vertical, que ce navigateur n'a pas.

## 3. Dia (actuel) — ce qui est vérifié

Sources : le [changelog officiel Dia](https://www.diabrowser.com/changelog) (entrées datées, Mac et Windows séparés) et les deux articles Arc → Dia ci-dessus. La page [diabrowser.com/help](https://www.diabrowser.com/help) ne contient pas d'article lisible sans JavaScript : l'aide de Dia se fait par chat dans l'application, selon la FAQ Arc.

### 3.1 Modèle et correspondance avec Arc (FAQ Arc, 2026-10-22)

| Arc | Dia | Nature |
|---|---|---|
| Favorites | **Pinned tabs en icônes seules**, « at the top of your window » | Nouveau nom |
| Pinned tabs | Pinned tabs avec leur titre | Identique |
| Folders | **Pinned Tab Groups** nommés | « Dia doesn't nest Tab Groups » |
| Live Folders | Live Groups | Nouveau nom |
| Sidebar | Deux dispositions : **top bar** (par défaut) ou sidebar, réglage *Settings > Tabs*, `Ctrl+Shift+S` sous Windows | Fonctionne autrement |

Dia garde **en plus** de vrais favoris, avec dossiers et barre de favoris. L'import depuis Arc reprend « bookmarks, history, passwords… », et les Profiles d'un même profil Arc partagent leurs favoris.

### 3.2 Entrées datées du changelog Dia

| Date et version | Entrée (citation ou résumé fidèle) | Intérêt pour ce projet |
|---|---|---|
| 2025-09-08, Mac 0.45.0 | « open bookmarks in background tabs by clicking with your middle mouse button (or Command+click) » | Clic milieu sur un favori |
| 2025-10-16, Mac 1.1.0 | Favori seul : `⌘`+clic l'ouvre en arrière-plan, `⌥`+clic en vue partagée | Modificateurs |
| 2026-01-08, Mac 1.13.1 | « When you save a bookmark, the interstitial now shows the **full folder tree inline** » | Popover d'ajout avec arbre de destination |
| 2026-01-08, Mac 1.13.1 | « Closed tab groups now **overflow into a simple button** instead of crowding your strip » ; un nouveau groupe passe directement en renommage | Débordement en un seul bouton |
| 2026-01-23, Mac 1.15.0 | « Bookmark Bar menus auto-size… up to 600px » | Largeur des menus de dossier |
| 2026-01-29, Mac 1.16.0 | Groupes épinglables « to the top of the sidebar or the **leading edge of the tab strip** » ; les groupes fermés vont dans *Recently Closed* | En mode top bar, l'épinglé est en tête de la bande d'onglets |
| 2026-02-12, Mac 1.18.1 | « The **Bookmarks Bar now only shows Tab Groups you've explicitly closed and moved there**, and only if they aren't open in another window » ; groupes Chrome importés comme favoris | Séparation stricte entre ouvert (onglets) et rangé (barre) |
| 2026-02-26, Mac 1.20.0 | « In split view, each pane now has its own navigation bar and **bookmark bar** » | La barre de favoris est une rangée sous la navigation |
| 2026-04-09, Mac 1.26.0 | Favoris : « adds clipboard support for URLs, and introduces **undo for bulk actions** » ; sync des favoris | Coller une URL ; annuler |
| 2026-07-09, Mac 1.39.0 | « Your bookmarks no longer shuffle themselves around when they sync » | L'ordre manuel est une donnée à protéger |
| 2026-09-03, Mac 1.47.1 | Les groupes de la barre apparaissent dans le menu *Bookmarks > Bookmarks Bar* ; `⌃⌘N` crée un groupe depuis les onglets sélectionnés | Accès par menu, pas seulement par la barre |
| 2026-10-01, Mac 1.51.0 | « Pinned Tabs now appear in **Tab Search** results » | Retrouver les épinglés |
| 2026-10-08, **Win 1.1.0** | « **Arc-Style Pinned Tabs** — pin tabs outside Favorites or groups » | L'épinglage Windows suit le modèle d'Arc |
| 2026-10-15, **Win 1.2.0** | « Adds a **“Back to Pinned”** button on the tab favicon, so pinned tabs can return to their original URLs » ; meilleur contraste des « bookmark-bar tab groups » dans la top bar | Un onglet épinglé garde une URL d'origine distincte de sa page actuelle |
| 2026-10-22, Mac 1.54.0 | « **Cmd+Shift+D pins tabs to Favorites** » | Raccourci propre à Dia, en conflit avec la convention Chrome (voir §4) |
| 2026-10-29, Mac 1.55 et 1.56 | « Closing pinned-group tabs **unloads their content without deleting them** », suppression par menu contextuel | Fermer un épinglé ne le supprime pas |
| 2026-11-05, **Win 1.5.0** et Mac 1.56.0 | « Bookmark context menus now offer **Open in New Tab** » ; Windows : « Opens background tabs on **middle-click** and correctly handles bookmarks, pinned tabs, tab groups… » | Menu contextuel minimal et clic milieu |

**Non vérifié :** le raccourci d'ajout d'un favori sous Dia Windows et la présence par défaut de la barre de favoris dans Dia Windows. Le changelog Windows ne mentionne que des groupes « bookmark-bar » et le menu contextuel.

**Inférence :** Dia étant basé sur Chromium (M156/M157 d'après le changelog Windows), `Ctrl+D` y ajoute probablement un favori comme dans Chrome. Cela n'a pas été observé.

## 4. Convention Windows de référence (aide Chrome)

Source : [Chrome keyboard shortcuts](https://support.google.com/chrome/answer/157179?hl=en&co=GENIE.Platform%3DDesktop) (consultée le 2026-11-08, Windows).

| Action | Raccourci Chrome |
|---|---|
| Ajouter la page aux favoris | `Ctrl+D` |
| Tous les onglets dans un nouveau dossier | `Ctrl+Shift+D` |
| Afficher ou masquer la barre de favoris | `Ctrl+Shift+B` |
| Gestionnaire de favoris | `Ctrl+Shift+O` |
| Focus sur les barres d'outils | `F6`, `Shift+Alt+T` |

**Vérifié dans le code local :** `src/main.cpp` (traduction des touches) n'utilise pas `Ctrl+D`, `Ctrl+Shift+D`, `Ctrl+Shift+B` ni `Ctrl+Shift+O`. `Alt+D` est déjà pris par la barre d'adresse, `Ctrl+H` par l'historique et `F6` par le passage page / barre. Le menu À propos liste ces raccourcis.

## 5. Comparaison

| Axe | Arc (historique) | Dia (actuel) | Chrome / Windows |
|---|---|---|---|
| **Ajouter** | Épingler ou mettre en favori depuis la barre latérale ; ajout de dossier par la Command Bar | Favori : interstitiel avec arbre de dossiers en ligne. Épingler : menu, ou `Cmd+Shift+D` sur Mac | `Ctrl+D`, popover sous l'étoile de l'adresse |
| **Dossiers** | Imbriqués, dans la barre latérale | Favoris : arbre. Groupes épinglés : un seul niveau | Arbre libre |
| **Débordement** | Défilement de la barre latérale | Groupes fermés regroupés dans un bouton ; menus de barre jusqu'à 600 px | Chevron `»` en fin de barre |
| **Retrouver** | Command Bar | Tab Search (inclut les épinglés), menu Bookmarks, Recently Closed | Barre d'adresse, gestionnaire `Ctrl+Shift+O` |
| **Raccourcis** | `Ctrl+T`, `Ctrl+L`, `Alt+D` | `Cmd+Shift+D` = épingler (Mac) ; `Ctrl+Shift+S` = disposition (Win) | `Ctrl+D`, `Ctrl+Shift+B`, `Ctrl+Shift+O` |
| **Vivant vs rangé** | Épinglé = ouvert, avec état | Épinglé : contenu déchargé à la fermeture, « Back to Pinned ». Live Groups mis à jour en continu. La barre ne montre que ce qui est fermé | Le favori est statique ; l'onglet épinglé est autre chose |

### Distinction à retenir : épinglé, vivant, favori

- **Onglet épinglé** (Dia et Arc) : un emplacement de travail. Il est dans la bande d'onglets, garde une URL d'origine, peut naviguer ailleurs puis revenir, et peut être déchargé sans être supprimé.
- **Groupe vivant** (Live Group/Folder) : son contenu est **calculé par un service** (PR GitHub, documents, réunions). Ce n'est pas une sauvegarde de l'utilisateur.
- **Favori** : une **référence rangée** (titre, URL, dossier). Il ne porte aucun état de page. Dia l'a rendu explicite en 1.18.1 : la barre ne montre que ce qui n'est pas ouvert ailleurs.

## 6. Recommandation pour ce navigateur

Contraintes locales, d'après [design-system.md](design-system.md) et le README :

- chrome horizontal Direct2D, contrôles de 32 DIP, rayon 6, Phosphor Bold 16 ;
- bande d'onglets de 128 à 32 DIP avec défilement et masques ;
- tiroirs de 268 DIP à droite pour Historique et Téléchargements, et de 332 DIP pour Réglages ;
- focus double 10 % / 50 %, UI Automation en place, mouvement réduit respecté ;
- aucun onglet épinglé aujourd'hui.

1. **Faire des favoris de vrais favoris, pas des onglets épinglés.** Le modèle d'Arc demande une barre latérale et disparaît le 22 décembre 2026. Dia lui-même garde les deux notions séparées. Les onglets épinglés et les groupes vivants restent **hors périmètre**, sans icône de punaise ambiguë.
2. **Ajouter.**
   - Une étoile dans le champ d'adresse, côté droit, sur une cible de 24 DIP, et `Ctrl+D`.
   - Un popover ancré sous l'étoile, sur le modèle du popover Téléchargements : nom modifiable, destination « Barre de favoris » par défaut, arbre de dossiers en ligne comme dans Dia 1.13.1, « Nouveau dossier », « Supprimer », Échap et Entrée.
   - Sur une page déjà en favori, l'étoile est pleine et le même popover sert à modifier.
   - Inactif sur l'accueil vide, comme Copier.
3. **Barre de favoris.**
   - Une rangée **optionnelle** sous la barre de navigation, comme la « bookmark bar » de Dia 1.20.0. Sa hauteur, environ 32 DIP, est à valider dans Paper.
   - Bascule par `Ctrl+Shift+B` et dans les Réglages.
   - Par défaut, inférence de produit : afficher la barre dès le premier favori, pour qu'une barre vide ne coûte pas de place.
   - Éléments : favicon 16 et titre tronqué, ou dossier avec chevron.
4. **Dossiers.**
   - Arbre autorisé dans les données, car Arc et Dia l'ont pour les favoris.
   - Menus en cascade natifs sur le modèle des menus de Réglages : 180 ms, flou de 3 DIP, instantanés en mouvement réduit. Largeur bornée, à l'image des 600 px maximum de Dia.
   - Protection contre les cycles au déplacement.
5. **Débordement.**
   - Un seul bouton `»` en fin de barre, qui ouvre un menu des éléments masqués, comme chez Chrome et comme le bouton unique de Dia 1.13.1.
   - Pas de défilement horizontal : il est réservé aux onglets.
   - En largeur étroite, les titres se réduisent avant de basculer dans `»`.
6. **Retrouver.**
   - Les favoris apparaissent dans les suggestions de la barre d'adresse, au-dessus de l'historique.
   - Un **tiroir Favoris** de 268 DIP, jumeau du tiroir Historique : recherche focalisée, dossiers repliables, `Suppr`, actions révélées au survol.
   - `Ctrl+Shift+O` ouvre ce tiroir, sans fenêtre de gestion séparée.
7. **Ouvrir.** Clic : onglet actif. Clic milieu ou `Ctrl`+clic : nouvel onglet en arrière-plan. Menu contextuel : « Ouvrir dans un nouvel onglet », « Modifier… », « Copier le lien », « Supprimer ». C'est le minimum livré par Dia Windows 1.5.0.
8. **Annuler.** Une suppression, y compris d'un dossier, se fait sans dialogue et propose une annulation (toast ou `Ctrl+Z` dans le tiroir), comme l'undo des actions groupées de Dia 1.26.0. L'ordre manuel ne doit jamais être réordonné par le système (Dia 1.39.0).
9. **Clavier et accessibilité.**
   - La barre de favoris est une barre d'outils UIA nommée « Favoris », atteinte par `F6` après la navigation, et parcourue avec Tab et les flèches.
   - Entrée ouvre l'élément, Maj+F10 ou la touche Menu ouvre le menu contextuel, `Alt+↑/↓` déplace.
   - Le popover annonce « Ajouté aux favoris » ou « Retiré des favoris ».
10. **Écarté pour l'instant.**
    - `Ctrl+Shift+D` reste réservé à la convention Chrome (« tous les onglets dans un dossier ») et n'est pas branché sur un épinglage.
    - Pas de synchronisation, ni de groupes vivants, ni de barre latérale.
    - Importer depuis Chrome ou Edge serait utile mais séparé.

## 7. Limites de cette recherche

- Rien n'a été installé, Dia comme Arc. Tous les comportements viennent du texte officiel ; aucune capture ni mesure de taille ou d'animation n'a été faite.
- Le changelog Dia décrit des changements, pas l'état complet. Une fonction jamais annoncée, par exemple le raccourci d'ajout sous Windows, reste **non vérifiée**.
- La plupart des entrées sur les favoris sont **Mac**. Leur présence sous Windows n'est confirmée que pour : Arc-Style Pinned Tabs (1.1.0), Back to Pinned et les groupes de barre de favoris (1.2.0), le polish des épinglés (1.4.0), Open in New Tab et le clic milieu (1.5.0).
- Les sources secondaires repérées (allthings.how, windowsreport.com, contextbolt.com) n'ont **pas** été utilisées comme preuve, par exemple pour l'imbrication sur 8 niveaux dans Arc.
