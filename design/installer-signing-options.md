# Installateur bloqué par Windows : options de signature

Recherche du 26 novembre 2026. Aucun achat, aucun compte créé, aucune inscription.
Chaque fait porte sa source et sa date (liste en bas). « Confirmé » = lu ce jour dans une page
officielle. « Déduit » = conclusion tirée de ces pages ou du code, pas encore prouvée par un essai.

## 1. Le problème

Sur certains PC Windows 11, le « Contrôle intelligent des applications » (Smart App Control) a
bloqué `browser-0.3.1062-win-x64-setup.exe` : « nous n'avons pas pu vérifier son éditeur ».
La fenêtre n'offre que « OK » et « Obtenir des applications du Store ».

Comment Windows décide (confirmé, Microsoft Learn, 30/09/2026 et Support Microsoft) :

1. Le service en ligne de Microsoft essaie de prédire si l'application est sûre.
2. S'il ne sait pas, l'application passe **seulement si elle est signée** avec un certificat
   émis par une autorité du « Trusted Root Program » de Microsoft.
3. Sinon elle est bloquée. Il n'existe **aucun moyen d'autoriser une seule application**
   (pas de « Exécuter quand même »).

Nos fichiers ne sont pas signés (design/updates.md : « No code signing certificate »). Chaque
version est un nouveau fichier inconnu, et nous en publions beaucoup (67 en novembre), donc
aucune n'a le temps de se faire une réputation (déduit). Le contrôle porte sur **tous les
exécutables**, pas seulement ceux qui viennent d'Internet (confirmé, Microsoft Learn,
04/05/2026) : le ZIP portable et les mises à jour sont concernés de la même façon.

Ne pas confondre avec SmartScreen, l'écran bleu « Windows a protégé votre ordinateur » : celui-là
laisse « Exécuter quand même ». Les PC sans Contrôle intelligent voient seulement SmartScreen.

## 2. Les options, pour un particulier en France

| Option | Ouvert à un particulier en France ? | Prix | Délai | Effet sur le Contrôle intelligent | Effet sur SmartScreen | Publication automatique à chaque fusion |
|---|---|---|---|---|---|---|
| **A. Certum « Open Source Code Signing » dans le cloud** | Oui : réservé aux particuliers | 49 € (durée à confirmer à la commande ; les certificats durent 459 jours au plus depuis 2026) | Non indiqué par Certum ; compter quelques jours à deux semaines (déduit) | Passe selon la règle officielle (certificat d'une autorité reconnue) ; à prouver par un essai | Avertissement au début, puis la réputation se construit et suit le certificat | Possible, mais par un montage non officiel (voir 4.A) |
| **B. SSL.com « IV » (particulier) + signature cloud eSigner** | Oui, pièce d'identité | 129 $/an + eSigner : 15 $/mois (240 signatures) ou 63,75 $/mois (1 200) | Validation 3 à 5 jours | Idem A | Idem A | Oui, officiellement prévu pour GitHub Actions |
| **C. Azure Artifact Signing (ex Trusted Signing)** | **Non.** Particuliers : États-Unis et Canada seulement. Entreprises : UE acceptée | 9,99 $/mois (5 000 signatures) | Validation 1 à 30 jours ouvrés, non accélérable | La voie « préférée » de Microsoft | Idem A | Oui, action GitHub officielle |
| **D. Certificat EV** (Certum dès 379 €, SSL.com « Sole Proprietor EV » 359 $/an) | Chez SSL.com oui, sans société ; ailleurs en général entreprise | 359 $ à 400 $+/an, plus la signature cloud | Plus long (vérifications renforcées) | Pas mieux qu'un certificat simple (déduit) | **Plus aucun avantage depuis 2024** | Comme B |
| **E. Microsoft Store, paquet MSIX** | Oui, compte gratuit, pièce d'identité + selfie | Gratuit, Microsoft signe | Revue jusqu'à 3 jours ouvrés **par version** | Signé par Microsoft : passe (déduit) | Aucun avertissement | **Non** : chaque version passe par la revue du Store, notre mise à jour maison ne sert plus |
| **F. Microsoft Store, notre installateur .exe listé** | Oui | Gratuit, mais **l'installateur doit déjà être signé** | Revue jusqu'à 3 jours ouvrés | Ne change rien : il faut A, B ou C d'abord | Pas d'avertissement pendant l'installation par le Store | Lien de version à déclarer à chaque version |
| **G. SignPath Foundation** (signature gratuite pour projets open source) | Dossier examiné, sans obligation d'accepter | Gratuit | Non indiqué | Idem A | Idem A | **Non** : approbation manuelle de chaque version ; l'éditeur affiché serait « SignPath Foundation » |

Détails utiles :

- **Nom affiché.** Avec A, Windows affiche « Open Source Developer, Prénom Nom » ; avec B et D,
  le nom civil. Aucune option pour particulier n'affiche une marque : il faut une société pour cela.
- **A, condition « open source ».** Certum demande l'adresse d'un projet open source public où le
  lien avec le demandeur est visible, une pièce d'identité et un justificatif de domicile. Il
  révoque le certificat s'il sert à signer un logiciel vendu. Le dépôt public sous licence MIT
  (VALXXII/valsbrowser) et la gratuité du navigateur vont dans ce sens. Point à dire franchement
  à Certum si la question vient : les versions publiées sortent du dépôt privé, avec des icônes
  sous licence que la copie publique remplace.
- **Clé toujours gardée sur du matériel certifié** (règle du secteur depuis le 1er juin 2023) :
  cloud du vendeur (A, B, C) ou clé USB. Une clé USB ne convient pas : la publication se fait sur
  les machines de GitHub, pas sur le PC.
- **Quotas.** 67 versions en novembre, 4 fichiers à signer par version : environ 270 à 400
  signatures par mois. A et C (5 000 par mois) suffisent. B au premier palier (240) ne suffit
  pas : il faudrait le palier à 63,75 $/mois, ou publier moins souvent.
- **D ne vaut pas son prix.** Microsoft écrit que payer un EV seulement pour éviter SmartScreen
  « n'est plus justifié ».
- **C deviendrait le meilleur choix avec une société** (environ 120 $/an, signature automatique
  prévue par Microsoft, nom de la société affiché). Les pages lues ne disent pas si une
  micro-entreprise est acceptée comme « entité légale ».

## 3. Recommandation : A, Certum « Open Source Code Signing » dans le cloud

C'est la seule option à la fois ouverte à un particulier en France, à petit prix (49 €), et
dont le quota tient notre rythme de publication. Elle répond à la règle écrite du Contrôle
intelligent et fait enfin progresser la réputation SmartScreen d'une version à l'autre.

À savoir avant de décider :

1. Le nom civil apparaît dans les fenêtres Windows.
2. La signature automatique passe par un secret de connexion rangé dans GitHub : montage courant
   mais non officiel chez Certum. Si cela ne tient pas, le repli est B.
3. Rien n'est prouvé tant qu'un fichier signé n'a pas été lancé sur un PC où le Contrôle
   intelligent est actif. À faire avant toute annonce.
4. L'avertissement SmartScreen bleu reste au début (« plusieurs semaines et des centaines
   d'installations », dit Microsoft), puis disparaît.
5. Avant d'acheter : faire d'abord l'essai gratuit du point 5 ci-dessous, avec un certificat de test.

## 4. Ce que chaque option demande à notre chaîne de publication

Chaîne actuelle (`.github/workflows/update-release.yml`) : tâche `build` sur Windows, sans aucun
secret (`tools/release/build.py`, tests, `tools/release/package.py`), puis tâche `publish` sur
Linux, seule à lire un secret, dans l'environnement `release` limité à `main`.

Fichiers à signer : `browser.exe`, `update-helper.exe`,
`assets/bin/browser-annotations-mcp.exe`, puis l'installateur complet.
`WebView2Loader.dll` est déjà signé par Microsoft (vérifié le 26/11/2026).

Commun à A, B et C :

- Signer les trois exécutables **avant** `package.py` : ce sont eux que le paquet de mise à
  jour, le ZIP et l'installateur embarquent. Sinon une mise à jour livrerait un `browser.exe`
  non signé, bloqué au lancement suivant sur les PC protégés (déduit).
- Signer l'installateur **après** l'ajout de son contenu. `package.py` calcule aujourd'hui
  `SHA256SUMS.txt` et `release-info.json` juste après l'écriture de l'installateur : ces
  calculs doivent passer après la signature.
- Garder la tâche `build` sans secret : ajouter une tâche `sign` sur Windows, dans
  l'environnement `release` (donc `main` seulement). Les branches restent non signées.
- Horodater chaque signature (`/tr … /td SHA256`), pour qu'elle reste valable après la fin du
  certificat.
- Refuser de publier si un exécutable n'est pas signé par le bon nom (contrôle dans `publish.py`).
- **Point à corriger dans l'installateur (déduit du code, à prouver).** Une signature se pose à
  une position multiple de 8 octets ; l'outil ajoute des zéros si besoin. Or
  `src/setup-payload.h` et `verify_setup` exigent que le bloc de fin soit collé à la signature.
  L'installateur 0.3.1062 fait 59 058 047 octets, pas un multiple de 8 : signé tel quel, il
  refuserait probablement de s'installer. Petite correction des deux lecteurs, avec un test.
- Le programme de mise à jour ne change pas : il vérifie toujours HTTPS, taille et SHA-256, et
  les empreintes sont calculées sur les fichiers signés. Il ne vérifie pas la signature
  (design/update-engine.md). Le faire serait un durcissement à part, pas nécessaire ici ; le
  code existe déjà pour WebView2 (`src/webview2-install.h`). Dans ce cas, comparer le nom du
  signataire, jamais l'empreinte du certificat, qui change à chaque renouvellement.
- Le fichier de publication est aussi copié vers le dépôt public : relire les règles de
  `tools/public-mirror/` avant de le modifier. Aucun secret ne part là-bas.

Propre à chaque option :

- **A. Certum.** La machine GitHub ouvre une session SimplySign avec un code à usage unique
  calculé depuis un secret, puis `signtool` signe. Outils tiers existants, aucun guide officiel
  Certum. Secrets : identifiant SimplySign et graine du code, dans l'environnement `release`.
- **B. SSL.com.** Action GitHub officielle `SSLcom/esigner-codesign`. Secrets : identifiant, mot
  de passe, identifiant de certificat, graine du code.
- **C. Azure.** Action officielle `azure/artifact-signing-action`. Certificats de 3 jours :
  horodatage indispensable. Secrets : identifiants d'une application Azure avec le rôle
  « Artifact Signing Certificate Profile Signer ».
- **E. Store MSIX.** Chantier, pas un correctif : nouveau paquet (identité, manifeste), envoi au
  Store, mise à jour maison coupée dans cette variante, vérification de tout ce qui écrit sur
  le disque (profils, agents, passerelle MCP). La règle du Store sur les navigateurs (moteur
  Chromium ou Gecko récent) est respectée par WebView2 (déduit).
- **F. Store .exe.** Installateur signé, installation silencieuse (notre `--unattended` existe,
  à vérifier contre leur exigence « aucune fenêtre »), adresse de version qui ne change jamais
  (nos adresses de version conviennent).
- **G. SignPath.** Construire depuis le dépôt public, approbation manuelle de chaque version.

## 5. Essai gratuit à faire avant tout achat

Signer une version de test avec un certificat fabriqué pour l'essai (sans valeur pour Windows),
puis vérifier que l'installateur signé s'installe toujours et que la mise à jour s'applique.
Cela prouve la chaîne, dont le point des 8 octets, sans rien dépenser. Seul le passage du
Contrôle intelligent demande ensuite le vrai certificat et un PC où il est actif.

## 6. Que dire aux utilisateurs bloqués maintenant

- Il n'existe pas de contournement par application (confirmé).
- Le ZIP portable **ne contourne pas** le blocage : le contrôle vise tous les exécutables
  (règle confirmée ; non essayé avec notre fichier).
- Seule issue aujourd'hui : désactiver le Contrôle intelligent (Sécurité Windows › Contrôle des
  applications et du navigateur › Paramètres du Contrôle intelligent des applications). **À ne
  pas conseiller à la légère** : cela retire la protection pour toutes les applications.
- Correction d'une idée reçue : sur un Windows 11 à jour, il peut être **réactivé** sans
  réinstaller (confirmé, Support Microsoft ; mises à jour de 2026 selon la presse). Sur un
  Windows pas à jour, la désactivation reste définitive jusqu'à réinitialisation.

Texte proposé :

> Windows bloque pour l'instant l'installation sur les PC où le « Contrôle intelligent des
> applications » est actif, parce que notre installateur n'est pas encore signé. Nous y
> travaillons. D'ici là, la seule façon d'installer est de désactiver ce contrôle dans Sécurité
> Windows, ce qui réduit la protection de tout le PC : nous vous conseillons plutôt d'attendre
> la version signée.

## 7. Sources

Toutes lues le 26/11/2026. La date entre parenthèses est celle de la page.

- Microsoft Learn, Artifact Signing, démarrage rapide (21/05/2026, mise à jour 20/11/2026) : pays, pièces demandées, délai. https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart
- Microsoft Learn, Artifact Signing, questions fréquentes (17/11/2026) : pas d'abonnement gratuit, réputation SmartScreen. https://learn.microsoft.com/en-us/azure/artifact-signing/faq
- Microsoft Learn, Artifact Signing, tarifs (06/01/2026) et intégrations (14/05/2026). https://learn.microsoft.com/en-us/azure/artifact-signing/how-to-change-sku et https://learn.microsoft.com/en-us/azure/artifact-signing/how-to-signing-integrations
- Microsoft Learn, Smart App Control, vue d'ensemble (30/09/2026) et signature (25/09/2026). https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/overview et https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/code-signing-for-smart-app-control
- Microsoft Learn, options de signature (29/10/2026) : tableau des options, EV sans avantage depuis 2024. https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- Microsoft Learn, réputation SmartScreen (04/05/2026) : contrôle de tous les exécutables, délai de réputation. https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- Support Microsoft, Smart App Control (sans date) : pas de contournement par application, désactivation, réactivation. https://support.microsoft.com/en-us/topic/what-is-smart-app-control-285ea03d-fa88-4d56-882e-6698afdb7003
- Microsoft Learn, Store : exigences pour .exe/.msi (mise à jour 24/08/2026), compte développeur (29/10/2026), certification (24/08/2026), règles du Store 7.20 (en vigueur le 22/10/2026). https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-package-requirements, https://learn.microsoft.com/en-us/windows/apps/publish/partner-center/open-a-developer-account, https://learn.microsoft.com/en-us/windows/apps/publish/store-policies
- Blog Windows Developer (19/05/2025) : compte gratuit pour les particuliers. https://blogs.windows.com/windowsdeveloper/2025/05/19/microsoft-store-expands-opportunities-for-windows-app-developers/
- Certum : prix et conditions. https://shop.certum.eu/open-source-code-signing-on-simplysign.html, https://shop.certum.eu/code-signing.html, https://support.certum.eu/en/code-signing-required-documents/
- SSL.com : particulier, EV sans société, eSigner. https://www.ssl.com/products/software-integrity/code-signing/iv/, https://www.ssl.com/products/software-integrity/code-signing/ev-sole-proprietor/, https://www.ssl.com/products/software-integrity/signing-service/
- Sectigo (durée de 459 jours ; pas de prix à un an ni d'offre claire pour particulier). https://www.sectigo.com/ssl-certificates-tls/code-signing
- SignPath Foundation, conditions. https://signpath.org/terms
- Presse, pour dater la réactivation du Contrôle intelligent (source secondaire). https://www.neowin.net/news/microsoft-removes-mandatory-clean-install-requirement-for-a-windows-11-exclusive-feature/

Non vérifié ce jour : la France dans la liste des pays du compte Store (page muette) ;
l'appartenance de Certum au programme de Microsoft (connue, non relue) ; le délai réel de
Certum ; l'accord de Certum pour une signature sans personne devant l'écran ; le comportement
réel du Contrôle intelligent face à un fichier signé par un certificat tout neuf (des
témoignages non vérifiés parlent de blocages malgré la signature).
