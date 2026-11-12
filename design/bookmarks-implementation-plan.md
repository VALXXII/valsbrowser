# Favoris : barre et dossiers — plan d'implémentation

Statut : **préparation uniquement**. Aucun code applicatif n'a été modifié. Ce plan accompagne le concept Paper (page « Favoris — flow », livré séparément dans `design/bookmarks-paper-flow.md`) et la recherche Dia/Arc (`design/bookmarks-research.md`). En cas d'écart sur l'apparence, la frame Paper fait foi ; en cas d'écart sur le comportement ou la faisabilité, ce plan fait foi.

Rédigé le 2026-11-08 sur la branche `VALXXII/Bookmarks` (base `dda7abb`). Les numéros de ligne renvoient à ce commit.

**Positionnement** (aligné sur `design/bookmarks-research.md` §6) :

- Ce sont de **vrais favoris** : des références rangées (titre, URL, dossier), sans état de page.
- **Ni onglets épinglés, ni groupes vivants, ni barre latérale** à la manière d'Arc ; aucune icône de punaise.
- Trois accès complémentaires :
  - l'étoile et Ctrl+D pour ajouter ;
  - une barre optionnelle sous la navigation ;
  - un tiroir « Favoris » de 268 DIP, jumeau de l'Historique, pour retrouver et gérer.
- L'ordre manuel n'est jamais réordonné par le système.

Principes repris des skills `apple-design` et `better-ui` (lus dans `~/.agents/skills/`) :

- **Agency, forgiveness** : supprimer est immédiat et réversible (annulation), jamais précédé d'une boîte de confirmation.
- **Purpose, restraint** : pas de bande vide à l'écran ; aucune fonction « bibliothèque » complète en v1.
- **Spatial consistency** : chaque popover ou menu part de son déclencheur (étoile, dossier, chevron) et repart par le même chemin.
- **Interactions fréquentes instantanées** : ouvrir un favori, survoler, passer d'un menu à l'autre. Le mouvement expressif est réservé aux moments rares (premier ajout, annulation).
- **Mouvement réduit** : fondu ou état final immédiat, jamais de déplacement ni de flou. Un repère statique (icône, libellé) double toujours l'animation.
- **Valeurs exactes du système** : appui à 0,96, radius 6 pour les boutons, coque de menu 10 = 6 + 4, focus 1 DIP (10 % / 50 %).

---

## 1. Architecture actuelle (références réelles)

| Sujet | Où | Ce qui compte pour les favoris |
|---|---|---|
| Fenêtre, commandes, persistance | `src/main.cpp` (1 735 lignes) | Tout l'hôte Win32 / WebView2 : `Command()` l.334, `Key()` l.335-354 (raccourcis), `Layout()` l.199, `Navigate()` l.256, `NewTab()` l.254, `Destination()` l.249 (texte tapé → URL). |
| Hauteur du chrome | `src/main.cpp:127` (`InvalidateChrome`, `S(84)`), `src/main.cpp:207` (`put_Bounds` à `S(84)`) | **Le haut de la page est codé en dur à 84 DIP.** Aussi `src/downloads.h:265` et `:302` (`top = 84`), `src/update-toast.h:13` (`topReserve = 84`). `src/history.h:134-148` (`viewTop == 84`) est une coordonnée interne au drawer : coïncidence, à ne pas toucher. |
| Rendu et hit-test du chrome | `src/chrome.h` : `Chrome::Build()` l.364-391, `Region` l.46, `FocusOrder()` l.392, `Hit()` l.393, `DrawFavicon` / `DrawSiteImage` l.120-124 | Régions identifiées par `int` : lettres (`'B'`, `'Y'`, `'D'`…), `TabBase=1000`, `CloseBase=2000` (`chrome.h:38`), commandes clavier `3000+` (`main.cpp:332`). La barre d'adresse : `bar={82,44,width-133,76}`, Actualiser à `bar.right-69`, Copier à `bar.right-28`. |
| Tokens | `src/design-system.h`, `design/design-system.md` | Surfaces (#171717, #292929, #333333, #3e3e3e), `Size::control` 32, `compact` 24, `icon` 16, `Radius::button` 6, espacements 4/6/8/16, `Toolbar::backLeft` 6, variantes `Ghost/Raised/Embedded`. |
| Bande d'onglets serrée | `design-system.h:89-151` (`TabStrip::Layout`) | Précédent pour la compaction et les masques de bord. Les favoris **ne la recopient pas** (voir §5). |
| Menu natif | `src/context-menu.h` : `Item` l.25 (`children` = sous-menu), `Layout` l.47-60, `Track()` l.412 (modal, renvoie l'id) | Sous-menus déjà gérés (survol 200 ms, Droite/Entrée immédiats), menus à description. **Manquent** : une icône par ligne, les modificateurs du clic (Ctrl / milieu) et toute exposition UI Automation. |
| Historique, URL | `src/address.h` : `Split` l.68, `Key` l.168 (comparaison normalisée), `Recordable` l.173 (http/https/file, < 4 096), `History::Write/Read` l.209-247, `SiteOf` l.86, `MatchText` l.162 | Le format texte versionné, la normalisation et les limites se réutilisent tels quels. |
| Persistance atomique | `main.cpp:356-374` (`SaveSession`, `SaveHistory`) | `.tmp` puis `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`, rien en mode smoke. `dataDir` résolu l.1440-1445 (portable ou installé). |
| Favicons | `main.cpp:378-419` (`SiteIcon`, `RememberSiteIcon`, `BundledIcon`) | Cache `<data>\favicons\<site>.png` + icônes embarquées `assets/sites`. Jamais de requête réseau à l'affichage. |
| Drawer historique | `src/history-panel.h` (`Drawer::Hooks` l.223) | Modèle pour une future bibliothèque : hooks `open(url,newTab)`, `remove`, `announce`. |
| Toast | `src/update-toast.h/.cpp` | Popup Direct2D non activante, coin bas-gauche, inset 14 DIP, `forceReducedMotion`. Base du toast d'annulation. |
| Réglages | `src/settings.h` (`State`, `Change`, `Write/Read` l.88-130), `src/settings-panel.h` | Ajout d'une clé = 4 endroits : `State`, `operator==`, `Write`, `Read` (+ `Change` si le panneau l'expose). |
| Accessibilité | `src/accessibility.h` : `Role` l.73 (`Button, Tab, TabItem, Edit, ToolBar, List, ListItem`), `Node` l.76 ; `main.cpp:1474-1500` (`AccessibleButton`, `AccessibleNodes`) | Pas de rôle `MenuItem`, `Group` ni `SplitButton` ; pas d'état développé/réduit. |
| Mouvement réduit | `src/motion.h:20-32` (`PreferredReducedMotion` = Windows ∪ réglage app), `main.cpp:711` | Toute nouvelle animation passe par `Tween::Set(..., reduced)` ou le ressort des drawers. |
| Glisser des onglets | `main.cpp:104` (`TabDrag`), `:232` (seuil `SM_CXDRAG/SM_CYDRAG`) | Même hystérésis pour réordonner les favoris. |
| Fenêtre minimale | `main.cpp:1634` (`ptMinTrackSize = 560 × 300` DIP) | Largeur la plus étroite à couvrir. |
| Icônes | `assets/icons/*.svg` + PNG 96→384, générés par `design/render-icons.cjs` | `folder`, `folder-closed`, `plus`, `caret`, `favicon` (globe), `close` existent. **Manquent** : `star` et `star-fill` (Phosphor Bold). |
| Tests | `tests/*_test.cpp`, `test-*.ps1`, `test.ps1` (smoke), `tests/update-integration/run_all.py` | Pattern : modèle pur testé sans HWND (`session_test`, `settings_test`), harness de rendu PNG (`test-history.ps1` → `build/history-qa`), client UIA réel (`test-accessibility.ps1`). |

Aucun fichier `AGENTS.md` ni `CLAUDE.md` dans le dépôt. Les conventions sont dans `README.md` et `design/design-system.md`.

---

## 2. Modèle de données — `src/bookmarks.h` (nouveau, C++17 pur)

Même contrat que `settings.h` et `address.h` : aucun HWND, Direct2D ni WebView2, pour qu'un test le couvre directement.

```cpp
namespace Bookmarks {
using Id = std::uint32_t;                 // 0 = aucun
constexpr Id BarRoot = 1, OtherRoot = 2;  // racines fixes, ni renommables ni supprimables
enum class Kind { Link, Folder };
struct Node {
    Id id = 0, parent = 0;
    Kind kind = Kind::Link;
    std::wstring title;                   // vide = afficher MatchText(url)
    std::wstring url;                     // vide pour un dossier
    std::int64_t added = 0;               // Unix ms
    std::vector<Id> children;             // ordre d'affichage (dossiers seulement)
};
struct Removed {                          // instantané pour l'annulation
    std::vector<Node> subtree;            // racine en premier, ordre préfixe
    Id parent = 0; std::size_t index = 0;
};
class Store {
public:
    static constexpr std::size_t Limit = 10000, MaxDepth = 8, TitleMax = 1024;
    const Node* Find(Id) const;
    const Node& Root(Id) const;
    Id Add(Id parent, std::size_t index, std::wstring title, std::wstring url, std::int64_t now);
    Id AddFolder(Id parent, std::size_t index, std::wstring title, std::int64_t now);
    bool Rename(Id, std::wstring title);
    bool SetUrl(Id, const std::wstring& url);           // refuse une URL non admise (§6)
    bool Move(Id, Id newParent, std::size_t index);     // refuse cycles et profondeur (§4)
    std::optional<Removed> Remove(Id);                  // refuse les racines
    bool Restore(const Removed&);                       // mêmes ids, même place si possible
    std::vector<Id> Matching(const std::wstring& url) const;  // par AddressBar::Key
    bool IsAncestor(Id ancestor, Id node) const;
    int Depth(Id) const;
    std::size_t CountLinks(Id folder) const;            // libellés « Ouvrir les 12 favoris »
    void Write(std::ostream&) const;
    static Store Read(std::istream&, bool* damaged = nullptr);
};
}
```

Choix :

- **Ids stables, jamais réutilisés** pendant la session (`next_` = max + 1). L'annulation et l'UIA (`AutomationId = Bookmark.<id>`) en dépendent.
- **Deux racines fixes** : « Barre de favoris » (`BarRoot`) et « Autres favoris » (`OtherRoot`). La seconde reçoit ce qui ne tient pas dans la barre par choix, et les nœuds orphelins à la lecture.
- **Doublons permis** dans des dossiers différents. L'étoile est pleine dès qu'un favori correspond (`Matching` non vide). Ctrl+D sur une page déjà enregistrée ouvre le plus récent en édition, sans en créer un second.
- **Index parent → enfants** gardé dans `children`. Pas de liste chaînée : les dossiers d'un humain restent petits, et le vecteur se sérialise dans l'ordre.

### Persistance : `<dataDir>\bookmarks`

Format texte UTF-8 versionné, comme `history` et `downloads` :

```
browser-bookmarks 1
<id>\t<parent>\t<L|F>\t<added>\t<titre>\t<url>
```

- **Ordre préfixe** (parent avant enfants, enfants dans leur ordre). `Read` exige que le parent soit déjà lu : un cycle est donc **impossible par construction** à la lecture.
- Lecture tolérante, ligne par ligne, comme `History::Read`. Ignorées : ligne mal formée, id dupliqué ou réservé, URL refusée (§6). Un parent inconnu ou trop profond rattache le nœud à `OtherRoot`. Titres nettoyés (`\t\r\n` → espace, comme `History::Clean`) et tronqués à 1 024. Arrêt à `Limit`.
- **Fichier abîmé ≠ fichier absent.** Les favoris sont des données personnelles, pas un cache : la perte coûte plus cher que pour l'historique.
  - Si l'en-tête est inconnu ou que `damaged` est levé, l'hôte copie le fichier en `bookmarks.damaged-<horodatage>` **avant** toute réécriture, puis l'écrit dans `prototype.log`.
  - À chaque lancement réussi, l'hôte garde une copie `bookmarks.bak` du dernier fichier lu sans erreur.
- **Écriture** : `SaveBookmarks()` dans `main.cpp`, calquée sur `SaveHistory` l.369-374 (`.tmp` + `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`). Elle est appelée après chaque mutation, regroupée sur 500 ms pendant un glisser et forcée à la fermeture. Rien en mode smoke (`if(smoke)return;`), comme les autres fichiers.
- `dataDir` couvre déjà les builds portable et installé (`main.cpp:1440-1445`). Le profil et la session restent intacts lors d'une mise à jour (`design/updates.md`).
- **« Effacer les données… » et « Effacer en quittant » ne touchent pas aux favoris.** La confirmation de `main.cpp:735` reste exacte telle qu'elle est. Le test l'impose (§10).

---

## 3. Ajouter, modifier, supprimer, annuler

### Ajouter

| Déclencheur | Effet |
|---|---|
| **Ctrl+D** (`Key()` : `ctrl && !shift && key=='D'` → nouvelle commande `'K'`). N'entre pas en conflit avec Alt+D (adresse). | La page active est enregistrée **tout de suite** dans le dernier dossier utilisé (par défaut `BarRoot`, en fin de liste), puis le popover d'édition s'ouvre, ancré à l'étoile. |
| **Étoile** dans la barre d'adresse (région `'K'`, 24 × 24, à gauche d'Actualiser) | Même chose au clic. Pleine et désignée « Modifier le favori » si la page est déjà enregistrée. |
| Clic droit sur la barre, sur un dossier ou dans le menu d'un dossier | « Ajouter la page ici », « Nouveau dossier… ». |
| Glisser l'onglet ou l'indicateur de l'adresse sur la barre | Phase 5 (optionnel). |

Désactivée, avec `Enabled('K') == false` comme Copier sur l'accueil vide : page d'accueil (`tab->home`), `about:blank`, schéma non admis (§6), navigation privée (quand elle existera).

**Géométrie de l'étoile.** Insérer un slot `compact` à `bar.right-99` et décaler `edit.right` de 30 DIP. C'est le précédent exact de l'arrivée de l'Historique (README : « le champ d'adresse raccourcit de 30 DIP pour lui »). L'étoile appartient à la page affichée : elle vit dans le champ, à côté de Copier, pas dans les boutons globaux de droite.

**Popover « Favori ajouté »** (nouveau `src/bookmark-popover.h`). Popup Direct2D possédée, même fabrication que le popover Téléchargements (`downloads.h:265`, ancre 8 DIP sous le bouton) :

- Largeur 320 DIP. Titre « Favori ajouté » ou « Modifier le favori » (14 medium).
- Champ **Nom** : EDIT natif, comme l'adresse ; texte présélectionné, focus à l'ouverture.
- **Destination** : arbre de dossiers **en ligne** dans le popover, comme l'interstitiel de Dia 1.13.1 (recherche §6.2). C'est une liste indentée de lignes de 32 DIP (`folder-closed` 16 + nom), avec au plus 5 lignes visibles et un défilement au-delà. Coche et fond `selected` sur le dossier choisi ; « Barre de favoris » par défaut. Le bouton « Nouveau dossier » sous la liste insère une ligne éditable dans le dossier sélectionné.
  - Pourquoi en ligne plutôt qu'un menu : choisir une destination reste visible et ne demande pas de second geste. Surtout, la liste s'expose en UIA avec les rôles déjà disponibles (`List/ListItem`, `accessibility.h:73`), alors que le menu natif n'est pas accessible aujourd'hui (§7).
- Actions : « Supprimer » (texte, à gauche) et « Terminé » (Raised, à droite).
- Entrée et « Terminé » valident. **Échap ferme en gardant le favori** (il est déjà créé) et annule seulement les modifications du popover. Clic dehors : valide.
- L'URL ne s'édite pas ici, mais dans « Modifier… » (popover agrandi d'un champ **Adresse**).
- Placement : ancré sous l'étoile, bord droit aligné sur elle, borné à 6 DIP des bords du client (§8).
- Hauteur variable (en-tête, Nom, arbre de 5 lignes au plus, actions), environ 340 DIP au maximum. Si la fenêtre est trop basse, l'arbre se réduit à 3 lignes.
- Annonces : « Ajouté aux favoris » et « Retiré des favoris » (formulation de la recherche).

### Modifier

- « Modifier… » (menu contextuel ou **F2** sur un favori focalisé) ouvre le même popover, avec Nom + Adresse + Dossier, ancré à l'élément de la barre. Pour un élément dans un menu de dossier, il s'ancre au dossier racine dans la barre.
- Dossier : « Renommer… » ouvre le popover réduit au seul Nom.
- Dans le tiroir Favoris (§5), F2 ou « Modifier… » ouvrent le même popover, ancré à la ligne.
- Une adresse invalide laisse « Terminé » actif. La validation affiche sous le champ « Adresse non valide » (texte secondaire, icône `warning`) et garde le focus dans le champ. Le message est annoncé par UIA. Pas de bordure rouge seule : un texte accompagne toujours la couleur.

### Supprimer et annuler

- Les commandes : « Supprimer » (menu contextuel, popover), **Suppr** sur un élément focalisé, et le bouton du popover.
- Effet : `Store::Remove` → `Removed` empilé dans une **pile d'annulation de session** (16 entrées, mémoire seulement), sauvegarde, puis toast d'annulation :
  - « Favori supprimé · **Annuler** »
  - « Dossier « Travail » supprimé (12 favoris) · **Annuler** »
- Toast : réutiliser la fabrique de `update-toast` (popup non activante, coin bas-gauche, inset 14 DIP, au-dessus de `ContentTop()`). Il **s'empile au-dessus** du toast de mise à jour s'il est visible. Durée 6 s, **suspendue au survol et au focus**. Un seul toast d'annulation à la fois : le plus récent remplace le précédent, qui reste annulable par Ctrl+Z.
- **Ctrl+Z** annule la dernière opération de favoris quand le focus est dans le chrome, la barre, le tiroir Favoris, un menu de favoris ou le toast. Jamais quand le focus est dans la page ou l'EDIT d'adresse : Ctrl+Z appartient alors au texte (`main.cpp:1263-1272`).
- `Restore` replace le sous-arbre au même parent et au même index (borné). Si le parent a disparu entre-temps, le sous-arbre va en fin de `OtherRoot`, et l'annonce le dit.
- **Pas de confirmation**, même pour un dossier plein : l'annulation suffit (principe Agency). Exception : « Ouvrir les N favoris » au-delà de 20 onglets demande confirmation. Le plafond actuel est de 128 onglets (`main.cpp:254`, `ReopenTab`).
- Annulables aussi : déplacement, renommage, création de dossier. Non annulable : l'ouverture.

---

## 4. Dossiers : imbrication et sûreté des cycles

- **Profondeur maximale 8** (`Store::MaxDepth`), racines comprises. Au-delà, des sous-menus en cascade sortent de l'écran à 560 DIP de large. `Move` et `AddFolder` refusent toute opération qui dépasserait la limite, avec l'annonce « Trop de niveaux de dossiers ». La lecture rattache les nœuds trop profonds à `OtherRoot` (§2).
- **Cycles** : à vérifier dans trois couches indépendantes.
  1. `Read` : un parent doit être lu avant son enfant, donc aucun cycle ne se charge.
  2. `Move(id, p)` : refus si `id == p`, si `IsAncestor(id, p)`, si `p` n'est pas un dossier, ou si `id` est une racine. `IsAncestor` remonte les `parent` avec un compteur borné à `MaxDepth + 1`, jamais une boucle `while` sans fin.
  3. **UI** : le sélecteur de dossier et les cibles de dépôt excluent le nœud lui-même et ses descendants. Ils ne sont pas grisés mais **absents**, pour ne pas proposer un choix impossible.
- Tous les parcours (écriture, comptage, menus) utilisent une pile explicite et un `std::vector<bool>` de visites. Aucune récursion non bornée sur des données lues du disque.
- Dossier vide : son menu montre une seule ligne inerte « (Vide) » et « Ajouter la page ici ».
- Un dossier de la barre s'ouvre en **menu déroulant** ancré sous lui (`ContextMenu::Track` avec `children`). Ses sous-dossiers sont des sous-menus. Pas de page de gestion en v1.
- En tête de chaque menu de dossier qui contient des liens : « Ouvrir les N favoris », suivi d'un séparateur.

### Évolutions nécessaires de `src/context-menu.h`

1. `Item::icon` : `std::shared_ptr<const FaviconImage>` ou nom de glyphe (`folder-closed`, `favicon`), dessiné dans une colonne de 16 DIP à 8 DIP du bord. Elle n'est réservée que si une ligne du menu a une icône.
2. **Modificateurs au choix** : `Track` renvoie aussi `{ctrl, shift, middle}`. Le clic milieu dans un menu est aujourd'hui ignoré ; il doit ouvrir en arrière-plan sans fermer le menu, comme dans tous les navigateurs.
3. **Menu contextuel dans un menu** : clic droit sur une ligne d'un menu de dossier → second `Track` imbriqué (Ouvrir, Nouvel onglet, Modifier…, Supprimer). C'est l'extension la plus risquée : elle se prototype seule en phase 4.
4. **UI Automation** (§7).

---

## 5. Barre : visibilité, débordement, ouverture

### Bande et `ContentTop()`

- **Préalable (phase 0)** : remplacer chaque `84` codé en dur listé en §1 par `ContentTop()`, qui vaut `84` sans barre et `84 + 36 = 120` avec. Concerne `main.cpp:127` et `:207`, `downloads.h:265` et `:302`, `update-toast.h:13`.
- Géométrie de la bande (DIP, client) :
  - Éléments `Size::control` 32 de haut, `y = 84 → 116`. La page commence à `120` : 4 DIP sous la bande, au lieu de 8 sous la barre d'outils, parce que la bande est une rangée secondaire.
  - Premier élément à `x = Toolbar::backLeft` (6), aligné sur Précédent.
  - Écart entre éléments `Space::tabGap` (6), comme onglets et boutons.
- Élément favori : variante **Ghost** (pas de fond au repos, `hover` #333333, `pressed` #3e3e3e, radius 6, appui 0,96). Favicon 16 à 8 DIP du bord, titre Inter Display 14 medium, texte secondaire. Le texte passe en primaire au survol, comme les titres de l'historique.
  - Largeur naturelle = 8 + 16 + 6 + titre + 8, **plafonnée à 160 DIP**, avec troncature par masque de fondu (même dégradé que l'adresse, `chrome.h:358-361`), sans « … ».
  - Titre vide : favicon seul, 32 × 32. Infobulle : titre complet + adresse lisible.
- Dossier : icône `folder-closed` 16, même gabarit, **plus un chevron discret** (décision du 2026-11-08, cohérente avec la maquette Paper et la recherche §6.3). Il signale qu'un clic ouvre un menu, et non une page.
  - **Chevron** : glyphe `caret` (pointe vers le bas) en 16 DIP, à 4 DIP après le titre, avec 6 DIP de marge droite. Opacité du rôle *muted* au repos, *secondary* au survol et au focus.
  - **Largeur** : `8 + 16 + 6 + titre + 4 + 16 + 6`. Le plafond (160) et le plancher (96) du §5 incluent le chevron : seul le titre rétrécit, et le chevron n'est jamais tronqué ni masqué par le fondu.
  - **Menu ouvert** : le dossier garde l'état « survol », comme les boutons de la barre d'outils qui ont ouvert un popover (`chrome.h:136`). Le chevron passe à l'opacité *primary* et pivote de 180° (pointe vers le haut) sur le ressort critique des interactions, 150 ms. Il revient par le même chemin à la fermeture. En mouvement réduit, la rotation est immédiate ; l'état visible (opacité, pointe) reste identique.
  - Le chevron ne porte pas de région propre : tout le dossier ouvre le menu, et le hit-test ne change pas.
- Régions : nouvelle plage `BookmarkBase = 4000` (+ index visible), plus `'O'` pour le chevron de débordement. Pas de collision avec `TabBase`, `CloseBase` ni les commandes `3000+` (`main.cpp:332`).
- Les drawers (Historique, Téléchargements, Réglages) **gardent `top = 84`**. Ils appartiennent aux boutons de la barre d'outils et recouvrent le bout droit de la bande pendant qu'ils sont ouverts. Le chevron masqué n'est alors pas focalisable (`Region::focusable=false`), comme un onglet sous un masque.

### Visibilité

Réglage `bookmarks-bar` dans `settings` (`settings.h`) : `auto` (défaut), `always`, `never`.

- `auto` : la bande apparaît dès que `BarRoot` contient un élément. **Aucune bande vide n'est montrée à un utilisateur qui n'a pas de favori** (Purpose). Le premier Ctrl+D la fait donc apparaître : c'est le retour d'état de l'ajout.
- **Ctrl+Maj+B** bascule entre `always` et `never` et l'annonce (« Barre de favoris affichée / masquée »). Aussi : menu principal, clic droit sur la bande, et ligne « Barre de favoris » (Toujours / Si elle contient des favoris / Jamais) dans la section Affichage du panneau Réglages.
- Bande masquée : les favoris restent accessibles par l'étoile, Ctrl+D, le menu principal « Favoris ▸ » (arbre complet en sous-menus) et les suggestions d'adresse (phase 6).
- **Apparition / disparition sans animation de la page.** Redimensionner le WebView à chaque image coûte cher, et le projet l'évite partout (README : « aucune fenêtre redimensionnée »). `put_Bounds` passe donc directement à la nouvelle valeur. Seuls les éléments de la bande entrent en fondu 120 ms, sans déplacement.

### Débordement

- Quand la place manque, deux étapes, dans cet ordre (recherche §6.5) :
  1. **Les titres se réduisent**, du plus long au plus court, du plafond de 160 DIP jusqu'à un plancher de **96 DIP** (troncature par fondu).
  2. Ce qui ne tient toujours pas passe dans le **menu de débordement**.
- Ni favicon seul ni défilement horizontal, contrairement à la bande d'onglets : un favori n'a pas d'état « sélectionné » à garder visible, et une rangée de favicons sans titre devient illisible.
- Bouton de débordement `'O'` : bouton Ghost 32 × 32, glyphe **`»`**, et non `caret`, pour ne pas le confondre avec le chevron des dossiers. Il est collé à droite (`width - 6 - 32`). Il n'existe que s'il y a débordement. La place qu'il prend est réservée **avant** de compter les éléments visibles, pour éviter qu'un élément saute à l'apparition du chevron.
- Son menu liste les éléments cachés dans l'ordre (dossiers = sous-menus), puis un séparateur et « Autres favoris ▸ » si `OtherRoot` n'est pas vide. Si rien ne déborde mais que `OtherRoot` contient des favoris, le chevron reste visible et n'ouvre que cette entrée.
- Fonction pure `Bookmarks::BarLayout(width, naturalWidths[]) → {visible count, overflow, rects}` (réduction puis débordement) dans `bookmarks.h`, testée sans Direct2D. Les largeurs de texte viennent de `Chrome::TextWidth` (`chrome.h:135`, mis en cache).

### Ouvrir

| Geste | Effet |
|---|---|
| Clic, Entrée | `Navigate(Active(), url)` (`main.cpp:256`), focus rendu à la page |
| Ctrl+clic, clic milieu, Ctrl+Entrée | `NewTab(url)` en arrière-plan, à droite de l'onglet actif |
| Maj+clic, Maj+Entrée | `NewTab(url)` au premier plan (pas de nouvelle fenêtre dans ce navigateur) |
| Clic sur un dossier, Entrée / Espace / Bas sur un dossier focalisé | Ouvre son menu |
| Clic droit, Maj+F10, touche Menu | Menu contextuel de l'élément |

Le menu contextuel d'un favori contient :

- Ouvrir
- Ouvrir dans un nouvel onglet
- Copier le lien
- Modifier…
- Déplacer vers ▸
- Supprimer

Celui d'un dossier contient :

- Ouvrir les N favoris
- Renommer…
- Nouveau dossier…
- Ajouter la page ici
- Déplacer vers ▸
- Supprimer

Celui de la bande vide contient :

- Ajouter la page
- Nouveau dossier…
- Afficher la barre de favoris ✓

### Tiroir « Favoris » (retrouver et gérer)

Jumeau du tiroir Historique (`src/history-panel.h`, `src/history.h`), dans un nouveau `src/bookmarks-panel.h` :

- 268 DIP de large, ancré au bord droit sous la barre d'outils (`top = 84`). Il partage le bord droit avec Historique, Téléchargements et Réglages : en ouvrir un ferme les autres.
- Il s'ouvre par **Ctrl+Maj+O** (recherche §6.6, convention Chrome), par « Gérer les favoris » en fin du menu de débordement et par le menu principal. Il n'y a pas de bouton supplémentaire dans la barre d'outils : le champ d'adresse a déjà perdu 30 DIP pour l'étoile.
- Contenu :
  - en-tête « Favoris » avec la croix ;
  - recherche focalisée à l'ouverture, avec la même règle de filtre que l'Historique (chaque mot dans le titre ou l'adresse) ;
  - sections repliables « Barre de favoris » et « Autres favoris », puis leurs dossiers imbriqués, repliables par leur chevron (indentation de 16 DIP par niveau) ;
  - lignes de 48 DIP (tuile 32 + titre + adresse lisible) ;
  - au survol, actions « + » (nouvel onglet) et « × » (supprimer), avec la même révélation que l'Historique.
- Clavier, en reprenant celui de l'Historique :
  - ↑ / ↓ entre les lignes, → / ← pour déplier ou replier un dossier ;
  - Entrée ouvre, Suppr supprime avec annulation, F2 modifie ;
  - **Alt+↑ / ↓ déplace** la ligne dans son dossier (recherche §6.9) ;
  - Échap efface la recherche, puis ferme le tiroir.
- UIA : `List/ListItem` existants, avec un nom et une position par ligne. **C'est le chemin accessible complet** pour les dossiers tant que le menu natif n'a pas d'UIA (§7).
- États vides : « Aucun favori » + « Ctrl+D pour ajouter la page » ; « Aucun résultat pour « … » ».

### Favicons

`SiteIcon(AddressBar::SiteOf(url))` (`main.cpp:397`) : cache disque, puis icônes embarquées, puis glyphe `favicon` (globe), et `file` pour une adresse `file://`. **Jamais de requête réseau** pour la barre. Ajouter un favori depuis un onglet garde le favicon que cet onglet a déjà reçu (`RememberSiteIcon`). **`ForgetSiteIcon` ne supprime plus le favicon d'un site qui a encore un favori** : il faut ajouter cette condition à `main.cpp:260` / `History::HasSite`.

---

## 6. Gestion des URL

- **Schémas admis** : `https`, `http`, `file` (= `AddressBar::Recordable`, `address.h:173`, limite 4 096). Refusés à l'ajout, à l'édition et à la lecture : `javascript:`, `data:`, `blob:`, `about:`, `edge:`, `chrome:`, `view-source:` et tout schéma inconnu. Les bookmarklets sont exclus volontairement : ils exécutent du code dans la page active.
- **Ajout depuis un onglet** : on enregistre l'URL telle que la donne WebView2 (`Source`, déjà dans `tab->uri`), sans réécriture. Le titre vient de la page ; s'il est vide, on prend `MatchText(url)` ou, pour un fichier, son nom (même règle que `HistoryView::MakeEntry`).
- **Saisie manuelle** (champ Adresse) :
  1. On rogne les espaces.
  2. Si le texte a un schéma, il doit être admis.
  3. Sinon, s'il ressemble à une adresse (`LooksLikeAddress`, `address.h:137`, ou règle de `Destination()` l.249-251 : un point et pas d'espace, ou `localhost`), on le préfixe par `https://`.
  4. Sinon, on refuse avec « Adresse non valide ».
  
  **Jamais de repli vers une recherche** : `Destination()` ferait une URL de moteur, ce qui est faux pour un favori. Il faut donc extraire de `Destination()` une fonction pure `AddressBar::Normalize(text) → optional<url>`, partagée et testée.
- **Comparaison** (étoile pleine, doublons) : `AddressBar::Key` (`address.h:168`). Schéma et hôte sans casse, `/` racine = rien. Le fragment `#` compte : deux ancres d'un document long sont deux favoris légitimes.
- **Affichage** : `ReadableHost` + `ReadableRest` pour les infobulles et l'UIA ; IDN affiché comme le rend déjà l'adresse ; adresse percent-décodée seulement pour l'affichage (`PercentDecode`), jamais pour l'enregistrement.
- **Navigation** : `Navigate()` reçoit l'URL enregistrée telle quelle. Le contrôle de schéma à la lecture garantit qu'aucun fichier modifié à la main ne fait exécuter `javascript:`.

---

## 7. Clavier et UI Automation

### Clavier

| Touche | Contexte | Effet |
|---|---|---|
| Ctrl+D | partout sauf dans un champ de saisie du popover | Ajouter / modifier la page |
| Ctrl+Maj+B | partout | Afficher / masquer la barre |
| Ctrl+Maj+O | partout | Ouvrir / fermer le tiroir Favoris |
| F6 | page → chrome | La bande devient un arrêt de F6 entre la barre d'outils et la page (`ToggleChromeFocus`, `main.cpp:334`) |
| Tab / Maj+Tab | chrome | **Un seul arrêt de tabulation pour toute la bande** (focus mobile) : on entre sur le dernier élément focalisé, sinon le premier. Ainsi, 40 favoris ne font pas 40 tabulations. |
| ← / → , Début / Fin | bande | Élément précédent / suivant, premier / dernier (chevron compris) |
| Entrée, Espace | élément | Ouvrir ; dossier ou chevron → menu, focus sur sa première ligne |
| Bas | dossier, chevron | Ouvrir le menu |
| Ctrl+Entrée, Maj+Entrée | lien | Nouvel onglet arrière-plan / premier plan |
| F2 | élément | Modifier / renommer |
| Suppr | élément | Supprimer (toast + annulation) |
| Ctrl+Maj+← / → | élément de la bande | **Déplacer** d'un cran. C'est l'alternative clavier au glisser, horizontale comme la bande. Elle est symétrique de Ctrl+Maj+PgPréc/PgSuiv pour les onglets (`main.cpp:340`). Décision arrêtée : Alt+↑/↓ est réservé à la liste verticale du tiroir. |
| Maj+F10, touche Menu | élément | Menu contextuel (déjà détecté, `main.cpp:1316`) |
| Ctrl+Z | chrome, bande, toast | Annuler la dernière opération de favoris |
| Échap | bande | Rend le focus à la page ; dans un menu, ferme le menu et rend le focus à son dossier (pas à la page) |

« Déplacer vers ▸ » (sous-menu des dossiers admissibles, §4) couvre les déplacements entre dossiers sans souris.

Modalité de focus inchangée : anneau 1 DIP blanc, 50 % au clavier et 10 % après un clic (`design-system.md`, `Focus`).

### UI Automation (`src/accessibility.h`, `main.cpp:1474+`)

- Nouveau nœud `Role::ToolBar` « Barre de favoris ». Décision arrêtée : ce nom complet distingue la barre du tiroir « Favoris ». Le nœud est placé après la barre « Navigation » dans l'ordre de lecture et absent quand la bande est masquée. Ses enfants :
  - Favori : `Button`, Name = titre, HelpText = adresse lisible, AutomationId `Bookmark.<id>`, position « 3 sur 12 ».
  - Dossier : `Button` + **ExpandCollapsePattern** (nouveau : `Role::MenuButton` ou état `expanded` sur `Node`), HelpText « Dossier, 5 éléments ».
  - Chevron : « Autres favoris (7) », ExpandCollapse.
  - Les éléments cachés par le débordement sont listés `offscreen=true` et restent invocables, comme les onglets masqués (README, « Lecteurs d'écran »).
- Étoile : `Button` « Ajouter aux favoris » / « Modifier le favori », AcceleratorKey « Ctrl+D », TogglePattern (Activé si la page est enregistrée). AutomationId `Bookmark.Star`.
- **Le menu natif n'a aujourd'hui aucune exposition UIA** (`context-menu.h` : aucune occurrence d'automation, de Narrateur ou de `NotifyWinEvent`). C'est un écart d'accessibilité existant qui touche déjà les menus contextuels. Ces travaux le rendent bloquant, puisque les dossiers s'ouvrent dans ce menu. Plan :
  1. Ajouter `Role::Menu` et `Role::MenuItem` à `accessibility.h`. Le menu expose son arbre comme fragment racine de sa propre popup, avec ExpandCollapse sur les lignes à sous-menu.
  2. Émettre `UIA_MenuOpenedEventId` / `MenuClosed`, et le focus sur la ligne active.
  
  Cette correction profite aussi aux menus existants (onglets, page, adresse).
- Popover : fenêtre avec Name « Favori ajouté ». Champs EDIT natifs nommés « Nom » et « Adresse » (comme l'adresse, `label` du `Host`). Bouton déroulant « Dossier » avec Value = nom du dossier.
- **Annonces** (notifications portées par le bouton étoile ou la bande, comme l'Historique) :
  - « Favori ajouté à Barre de favoris »
  - « Favori supprimé. Ctrl+Z pour annuler »
  - « Favori restauré »
  - « Déplacé en position 4 sur 12 »
  - « Barre de favoris masquée »
  - les erreurs de validation

---

## 8. Mouvement, mouvement réduit, largeur étroite

### Mouvement (normal / réduit)

| Moment | Normal | Réduit (`PreferredReducedMotion()`) |
|---|---|---|
| Survol, appui d'un élément | fondu 120 ms, appui 0,96 en 150 ms (`Interaction`) | fondu 120 ms, pas d'échelle |
| Menu de dossier / débordement | tel quel : ouverture instantanée (geste fréquent, `context-menu.h:10`) | idem |
| Popover d'ajout | 180 ms : fondu + glissement de 6 DIP depuis l'étoile + flou 3 → 0 (valeurs des menus à choix, `context-menu.h:59`). Sortie : 140 ms, même chemin, plus courte. | apparition / disparition immédiates |
| Étoile qui se remplit | fondu enchaîné contour → plein : échelle 0,25 → 1, opacité, flou 4 → 0, ressort 300 ms sans rebond (`better-ui`) | fondu 100 ms |
| Bande qui apparaît | page immédiatement à `ContentTop()` ; éléments en fondu 120 ms | immédiat |
| Nouvel élément dans la bande | fondu 180 ms à sa place ; les voisins glissent sur un ressort critique (réponse 0,24 s) depuis leur position **actuelle** (interruptible) | immédiat |
| Glisser pour réordonner | suivi 1:1 du pointeur après le seuil `SM_CXDRAG` ; les voisins s'écartent sur le même ressort ; au lâcher, l'élément part de sa position à l'écran | les voisins sautent à leur place ; l'élément suit le pointeur (manipulation directe, pas une animation) |
| Toast d'annulation | entrée 240 ms depuis le bas (12 DIP + fondu), sortie 150 ms | fondu 100 ms |
| Suppression d'un élément | sortie 150 ms (fondu + −4 DIP), puis resserrement des voisins sur ressort | immédiat |

Images cadencées par le compositor clock existant (`frame-clock.h`), **aucun timer au repos**. Une seule exception : le compte à rebours de 6 s du toast, suspendu au survol, déjà admis pour les téléchargements visibles. Seul le rectangle de la bande est invalidé quand elle seule bouge (même règle que la barre d'adresse, README « Cadencement »).

### Largeur étroite (minimum 560 DIP, `main.cpp:1634`)

- À 560 DIP, la bande offre `560 − 6 − 6 − 32 (chevron) − 6 = 510` DIP, soit environ 3 éléments de 160 ou 5 à 6 titres courts. Le reste part dans le débordement ; rien n'est compacté ni chevauché.
- Le champ d'adresse perd 30 DIP pour l'étoile. À 560 DIP, il garde `560 − 82 − 133 − 30 ≈ 315` DIP (à vérifier contre `Address::textInset` et l'indicateur de sécurité). C'est le point le plus serré : **critère d'acceptation explicite** au §10.
- Popover (320 DIP) : ancré sous l'étoile, bord droit aligné sur elle. Si la place manque, `left = max(6, right − 320)`, et il n'est jamais plus large que `width − 12`.
- Menus de dossier : `ContextMenu` borne déjà au moniteur. Les sous-menus passent à gauche quand la droite manque : à vérifier à 560 DIP sur 8 niveaux.
- Toast : 14 DIP du bord ; largeur maximale `width − 28`, texte sur une ligne ; le nom d'un dossier long est tronqué par un fondu.
- DPI : 96, 144 et 192 couverts par les harness de rendu, comme les autres composants.

---

## 9. Ordre d'implémentation incrémental

Chaque phase est fusionnable seule (PR, tests verts), sans régression visible pour qui n'a pas de favoris.

0. **`ContentTop()`** : remplacer les `84` listés en §1, sans changement visuel. Test de géométrie : avec la bande à 0, les bounds WebView et le haut des drawers sont identiques au pixel près.
1. **Modèle pur** : `src/bookmarks.h` (Store, Read/Write, Move/Remove/Restore, IsAncestor, BarLayout) + `AddressBar::Normalize` extrait de `Destination()`. Test `tests/bookmarks_test.cpp` lancé par `test.ps1` comme `session_test` et `settings_test`. Pas d'UI.
2. **Ajout minimal** : chargement et sauvegarde dans `main.cpp` (`bookmarksFile`, fichiers `.bak` / `.damaged`), icônes `star` / `star-fill` (via `render-icons.cjs`), région `'K'`, Ctrl+D, étoile pleine ou vide selon `Matching`. Pas encore de popover : l'ajout va dans `BarRoot`, avec l'annonce.
3. **Bande** : rendu dans `Chrome::Build/Paint`, réglage `bookmarks-bar`, Ctrl+Maj+B, ouverture (clic, Ctrl, milieu, Maj), favicons, débordement, clic droit (sans « Déplacer vers »). Harness `test-bookmarks.ps1` → PNG dans `build/bookmarks-qa`.
4. **Dossiers et édition** : `ContextMenu` (icônes, modificateurs, menu contextuel imbriqué), menus de dossier, popover d'ajout et de modification (`bookmark-popover.h`), suppression + toast d'annulation + Ctrl+Z.
5. **Réordonner** : glisser dans la bande et vers un dossier (survol d'un dossier 500 ms → ouverture), Ctrl+Maj+← / →, « Déplacer vers ▸ ».
6. **Tiroir Favoris** : `bookmarks-panel.h`, Ctrl+Maj+O, recherche, dossiers repliables, Alt+↑/↓, harness PNG. Il peut précéder la phase 5 si le menu natif reste inaccessible : c'est alors le chemin accessible vers les dossiers.
7. **Accessibilité** : bande + étoile dans `AccessibleNodes`, UIA du menu natif (`Role::Menu/MenuItem`, ExpandCollapse), annonces. Extension de `test-accessibility.ps1`. Commencer plus tôt si possible : la phase 4 sans menus accessibles ne doit pas être livrée seule à un utilisateur de lecteur d'écran.
8. **Retrouver** (optionnel) : favoris dans les suggestions d'adresse (`Kind::Bookmark` au-dessus de l'historique dans `Suggest`, `address.h:296`), README et design-system mis à jour.

Hors périmètre v1 : onglets épinglés et groupes vivants (notion distincte, recherche §5), gestionnaire en fenêtre séparée, import / export HTML, synchronisation, favoris par onglet épinglé (voir la recherche Dia/Arc), séparateurs, tags.

---

## 10. Critères d'acceptation vérifiables

**Modèle (`tests/bookmarks_test.cpp`, sans HWND)**

1. Écrire puis relire un arbre de 3 niveaux restitue ids, ordre, titres avec accents, et URL `file:///C:/notes%20x.html` identiques.
2. `Move(A, descendant de A)`, `Move(A, A)`, `Move(BarRoot, …)`, `Remove(OtherRoot)` renvoient `false` et laissent l'arbre inchangé (comparaison d'égalité complète).
3. Un fichier où un enfant précède son parent, ou forme un cycle `1→5→6→5`, se lit sans boucle (délai < 100 ms sur 10 000 lignes) ; les nœuds fautifs sont dans `OtherRoot` et `damaged` est levé.
4. Profondeur : un `AddFolder` qui créerait le 9ᵉ niveau échoue ; un fichier à 12 niveaux est ramené à 8.
5. Lignes `javascript:alert(1)`, `data:text/html,…`, `about:blank` et URL de 5 000 caractères : ignorées à la lecture, refusées par `Add` et `SetUrl`.
6. `Normalize` : `"exemple.fr"` → `https://exemple.fr`, `"localhost:3000"` → `https://localhost:3000`, `"deux mots"` → refus (pas d'URL de moteur), `" HTTPS://A.fr/ "` → conservé.
7. `Remove` puis `Restore` d'un dossier de 12 favoris le remet au même index avec les mêmes ids. Si son parent a été supprimé entre-temps, il arrive en fin de `OtherRoot`.
8. `BarLayout` à 560, 800 et 1 180 DIP : aucun titre sous 96 DIP ; le débordement n'apparaît qu'une fois tous les titres au plancher ; aucun rectangle ne dépasse `width − 6` ; chevron présent si et seulement s'il y a débordement ou que `OtherRoot` n'est pas vide ; ajouter le chevron ne déplace aucun élément resté visible.

**Hôte et rendu (`test-bookmarks.ps1`, harness PNG à 96 / 144 / 192 DPI)**

9. Sans favori et en mode `auto` : bounds WebView à `y = 84`, rendu de la barre d'outils identique au rendu de référence d'avant la phase 0.
10. Avec un favori : bande visible, page à `y = 120`, premier élément à `x = 6` (aligné sur Précédent), écart de 6 DIP entre éléments. Un dossier dessine son chevron à 4 DIP après le titre, entier même au plancher de 96 DIP. Le bouton de débordement montre `»` et jamais `caret`. Rendus PNG « dossier fermé / menu ouvert » : chevron vers le bas en *muted*, puis vers le haut en *primary* ; en mouvement réduit, l'état final apparaît dès la première image.
11. À 560 DIP : champ d'adresse ≥ 300 DIP de texte utile, popover entièrement dans le client, aucun chevauchement de régions (`Hit` unique pour chaque point).
12. Mouvement réduit forcé : chaque transition du §8 rend son état final dès la première image (comparaison d'images à t = 0 et t = fin).
13. Au repos, bande visible : zéro réveil du frame clock sur 2 s (même mesure que `display-pacing.md`).

**Bout en bout (`test.ps1`, smoke, profil vierge, aucun fichier de favoris lu ni écrit)**

14. Ctrl+D sur une page locale → étoile pleine, bande visible, popover focalisé sur Nom ; Échap → popover fermé, favori conservé.
15. Suppr sur le favori focalisé → toast visible ; Ctrl+Z → favori revenu à son index ; toast fermé.
16. « Effacer les données… » ne modifie pas le fichier `bookmarks` (empreinte identique avant / après).
17. Fichier `bookmarks` tronqué à la moitié d'une ligne : le lancement réussit, un fichier `bookmarks.damaged-*` existe, les lignes valides sont chargées.

**Accessibilité (`test-accessibility.ps1`, vrai client UIA)**

18. La barre « Barre de favoris » contient N boutons nommés par leurs titres, `AutomationId` `Bookmark.<id>` ; dossier avec ExpandCollapse ; Invoke ouvre bien la page.
19. Étoile : Name « Ajouter aux favoris », AcceleratorKey « Ctrl+D », Toggle passe à On après Invoke.
20. Ouvrir un dossier depuis UIA expose un `Menu` avec ses `MenuItem` et déclenche `MenuOpened` ; Échap rend le focus UIA au dossier.
21. Un seul arrêt de Tab pour la bande ; ← / → / Début / Fin parcourent les éléments, chevron compris ; Ctrl+Maj+→ annonce « Déplacé en position k sur n ».
22. Tiroir : Ctrl+Maj+O l'ouvre avec la recherche focalisée ; Alt+↓ déplace la ligne et l'ordre persiste après relance ; la liste UIA donne nom et position de chaque ligne.

**Manuel (non automatisable, à consigner)** : Narrateur et NVDA sur le flux complet ; glisser au trackpad et à la souris ; contraste mesuré des titres de la bande (secondaire #949494 / #171717 attendu ≥ 4,5:1, à mesurer sur rendu réel) ; mode Contraste élevé de Windows.

---

## 11. Risques et questions ouvertes

- **Accessibilité du menu natif** : c'est le chantier le plus lourd (§7, phase 7). Le tiroir Favoris (phase 6) fournit d'ici là un chemin complet aux lecteurs d'écran ; le sélecteur en ligne du popover n'en dépend pas non plus.
- **Décisions arrêtées par le coordinateur le 2026-11-08** (elles ne sont plus ouvertes) :
  - bande de **36 DIP** : éléments de 32, page à `y = 120`, 4 DIP sous la bande ;
  - **Ctrl+Maj+← / →** pour déplacer dans la bande, **Alt+↑ / ↓** dans le tiroir ;
  - nom UIA **« Barre de favoris »** ;
  - **chevron discret sur les dossiers** de la bande, qui pivote pendant que le menu est ouvert (§5). Le bouton de débordement utilise `»` pour s'en distinguer.
- **Visibilité `auto`** : elle diffère de Chrome (barre sur la page Nouvel onglet seulement). Elle est cohérente avec la règle « pas de bande vide ». Décision produit à valider.
- **Clic milieu dans `ContextMenu`** : il demande de modifier une boucle modale partagée par tous les menus. Il faut des tests de non-régression des menus d'onglet, de page et d'adresse.
- **Langue** : les libellés de ce plan suivent les panneaux en français (Réglages, menus). Les drawers Téléchargements et Historique sont en anglais (« Download history », « History »). Les favoris suivent le français, comme la majorité du chrome.
