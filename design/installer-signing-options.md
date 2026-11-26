# Installateur bloqué par Windows : options de signature

Recherche du 26 novembre 2026. Aucun achat, aucun compte créé, aucune inscription.
Mise à jour du même jour : le propriétaire a choisi l'option A (Certum) ; les parties 4 à 6 décrivent la
chaîne de signature préparée, ses essais et ce qu'il lui reste à faire.
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
5. L'essai gratuit avec un certificat de test est fait : voir la partie 5.

## 4. La chaîne de signature : prête, essayée avec un certificat de test, coupée

Décision du propriétaire (26/11/2026) : option A, Certum. Tout ce qui suit est écrit et essayé avec un
certificat de test fabriqué pour l'occasion. **Rien n'est signé pour de vrai tant que l'interrupteur
n'est pas mis.**

### Ce qui est signé, dans quel ordre

Tout se fait dans `tools/release/package.py` (option `--sign-name`), avec `tools/release/sign.py`.

1. **Les programmes du navigateur** : `browser.exe`, `update-helper.exe`,
   `assets/bin/browser-annotations-mcp.exe`. Ils sont signés dans la copie de préparation, avant que le
   paquet de mise à jour, le ZIP et l'installateur soient fabriqués : les trois contiennent donc les
   mêmes fichiers signés. `WebView2Loader.dll` est déjà signé par Microsoft : il est seulement contrôlé.
2. **Le désinstalleur.** L'installateur dépose sur le PC une copie de lui-même sans son contenu : c'est
   le désinstalleur. Une signature posée sur l'installateur entier ne couvre pas ce petit fichier, qui
   serait donc bloqué par le Contrôle intelligent. Il est signé à part et rangé dans l'installateur.
3. **L'installateur complet**, en dernier.
4. **Contrôle de chaque signature** par la vérification de Windows lui-même, puis seulement calcul des
   empreintes publiées (`SHA256SUMS.txt`, `release-info.json`, qui note « signé » et le nom du signataire).
5. **Publication** : `tools/release/publish.py --require-signer` refuse tout ce qui n'est pas signé par
   le bon nom, ou signé avec un certificat de test.

Chaque signature est horodatée (`http://time.certum.pl`) : elle reste valable après la fin du certificat.

### Deux défauts de l'installateur corrigés au passage

- **Signé, il ne s'installait plus.** Prouvé : l'installateur de la version actuelle, une fois signé,
  répond « retéléchargez-le » dans 7 tailles de fichier sur 8 (l'outil de signature ajoute jusqu'à 7
  octets avant sa signature, et l'installateur ne retrouvait plus son contenu). Son bloc de fin est
  maintenant toujours aligné : il se lit pareil avec ou sans signature.
- **Le désinstalleur n'était pas signé** (point 2 ci-dessus). L'installateur vérifie que le désinstalleur
  qu'il transporte est bien lui-même, signé, avant d'écrire quoi que ce soit.

### L'interrupteur

Variable du dépôt GitHub `RELEASE_SIGNING` (Settings › Secrets and variables › Actions › Variables) :

| Valeur | Effet |
|---|---|
| absente (aujourd'hui) | Rien ne change : aucune signature, publication comme avant. |
| `rehearsal` | Répétition : GitHub ouvre la session Certum, signe, contrôle, et garde le résultat de côté. La version **publiée reste non signée**, et un échec de la répétition ne bloque aucune publication. |
| `on` | Seuls les fichiers signés sont publiés. Pas de signature valable, pas de publication. |

Dans `.github/workflows/update-release.yml` : la tâche `build` ne change pas et ne voit aucun secret ;
une tâche `sign` (Windows, environnement `release`, donc `main` seulement) ouvre la session, lance
`package.py --sign-name`, ferme la session ; la tâche `publish` prend les fichiers signés quand la
valeur est `on`.

### Les secrets : leurs noms, jamais leurs valeurs

| Nom | Où le créer | Ce que c'est |
|---|---|---|
| `CERTUM_SIMPLYSIGN_ID` | Settings › Environments › `release` › Environment secrets | L'identifiant du compte SimplySign (l'adresse e-mail du compte Certum). |
| `CERTUM_SIMPLYSIGN_TOTP_SECRET` | même endroit | Le secret contenu dans le QR code d'activation (le texte qui commence par `otpauth://`, ou sa partie `secret`). |
| `RELEASE_SIGNER_NAME` | Settings › Secrets and variables › Actions › Variables | Le nom exact écrit sur le certificat (celui que Windows affichera). |

### Comment GitHub atteint la clé rangée chez Certum

- **Ce que Certum prévoit (officiel).** La clé ne quitte jamais Certum. Sur un PC, on ouvre le logiciel
  SimplySign Desktop, on tape son identifiant et un code à 6 chiffres donné par l'application SimplySign
  du téléphone ; le certificat apparaît alors dans Windows et l'outil de signature s'en sert. Certum ne
  décrit aucune méthode sans personne devant l'écran.
- **Ce que nous faisons (non officiel, comme les projets publics qui signent avec Certum depuis
  GitHub).** `tools/release/simplysign_session.ps1` installe SimplySign Desktop sur la machine de
  GitHub (fichier figé : version, empreinte et signature de son éditeur vérifiées avant de le lancer),
  calcule lui-même le code à 6 chiffres à partir du secret du QR code, et remplit la fenêtre de
  connexion. Une seule tentative par publication, jamais deux sessions en même temps.
- **Ce qui est prouvé et ce qui ne l'est pas.** Le calcul du code est vérifié contre les valeurs de
  référence de la norme. La connexion elle-même n'a pas pu être essayée : il faut le vrai compte. La
  première mise au point se fera en répétition.
- **Risques.**
  - Le secret du QR code et l'identifiant suffisent pour signer à ta place. Ils ne sont lus que par la
    tâche `sign`, sur `main`. S'ils fuient : demander un nouveau QR code à Certum.
  - Certum peut changer son logiciel et casser la connexion automatique : la publication signée s'arrête
    alors (rien de non signé ne sort quand l'interrupteur est sur `on`) jusqu'à correction.
  - Trop de connexions refusées peuvent bloquer le compte : d'où la tentative unique.
  - Quota : 5 000 signatures par mois, 4 par version : large.
- **Si la connexion automatique ne tient pas.** Deux replis : signer sur le PC de Val, SimplySign
  ouvert avec le téléphone, ce qui oblige à regrouper les publications (une par nuit, par exemple) ; ou
  passer à SSL.com (option B), prévu officiellement pour GitHub, plus cher.

### Si une autre option était choisie plus tard

Le reste de la chaîne ne change pas (`package.py --sign-name` signe avec le certificat présent dans
Windows) ; seule l'ouverture de session diffère :
- **B. SSL.com** : action GitHub officielle `SSLcom/esigner-codesign` (identifiant, mot de passe,
  identifiant de certificat, secret du code à usage unique).
- **C. Azure Artifact Signing** (avec une société) : action officielle `azure/artifact-signing-action` ;
  certificats de 3 jours, donc horodatage indispensable (déjà fait).
- **E. Store MSIX** : un chantier à part (nouveau paquet, envoi au Store, mise à jour maison coupée
  dans cette variante).
- **F. Store avec notre .exe** : il faut déjà un installateur signé, une installation silencieuse (notre
  installateur n'en a pas dans les versions publiées : le mode sans fenêtre n'existe que dans les
  versions de test) et une adresse de version qui ne change jamais (les nôtres conviennent).
- **G. SignPath** : construire depuis le dépôt public, approbation manuelle de chaque version.

## 5. Essais faits avec le certificat de test

Tous hors écran, dans des dossiers jetables ; le vrai navigateur installé n'est jamais touché.

- **Installateur signé** (`tests/setup/run_tests.py`, cas `signed_release`, 84 vérifications ; la suite
  complète : 17 cas verts). Une version signée s'installe ; une version signée plus récente arrive par
  le vrai moteur de mise à jour ; l'installateur suivant passe par-dessus ; la désinstallation tourne
  depuis une copie signée. Après chaque étape, chaque programme installé est contrôlé par Windows :
  signature valable, bon signataire. Un octet changé dans un fichier signé est vu par Windows.
- **Preuve inverse** : les mêmes essais sur le code actuel de `main` échouent (l'installateur signé ne
  trouve plus son contenu).
- **Outils de publication** (`tests/update-integration/test_release_tools.py`, 74 tests verts) : format
  du fichier pour les 8 alignements possibles, désinstalleur transporté, refus de publier du non signé
  quand c'est demandé, propriétés de sécurité du fichier de publication (secrets dans la seule tâche
  `sign`, interrupteur coupé par défaut).
- **Avec le certificat de test, Windows répond « racine non reconnue »** : c'est attendu, et accepté
  dans les tests seulement. Un certificat de test ne peut pas signer une version destinée au public
  (`package.py` le refuse).

- **Vraie compilation complète** (version de développement, 26/11) : `package.py` a signé les 5 fichiers
  (les 3 programmes, le désinstalleur, l'installateur) en 30 secondes, horodatés par le service de Certum ;
  le ZIP, le paquet de mise à jour et l'installateur contiennent bien les fichiers signés ;
  `WebView2Loader.dll` garde la signature de Microsoft.
- **Suite complète après rebasage sur la dernière version** (bureau caché) : installateur 574 vérifications,
  mise à jour, démarrage : tout vert ; tests natifs 117 réussis, 2 échecs identiques à ceux de `main`
  (captures d'écran impossibles sur un bureau caché).

Pas encore fait :
- Installer une vraie version signée : le programme de mise à jour d'une vraie version écrit dans le
  registre de l'utilisateur, donc cet essai se fait sur une machine propre ou pendant la répétition,
  pas sur ce PC.
- Le passage du Contrôle intelligent : il faut le vrai certificat et un PC où il est actif.
- La connexion automatique à Certum depuis GitHub : il faut le vrai compte (répétition).

## 6. Ce que Val doit faire

Dans l'ordre. Rien ici ne se délègue : c'est ton identité et ta clé.

1. **Finir la commande chez Certum** (« Open Source Code Signing in the Cloud »).
2. **Faire vérifier ton identité**, dans ton compte Certum, rubrique des produits de sécurité des
   données, « activer » le certificat :
   - une pièce d'identité (carte d'identité ou passeport). Certum recommande sa vérification
     automatique : photo du document, recto et verso ;
   - un justificatif de domicile à ton nom (facture d'électricité, d'eau, de téléphone…) ;
   - l'adresse du projet open source : `https://github.com/VALXXII/valsbrowser`. Certum doit y voir le
     lien entre toi et le projet. Si ton nom civil n'apparaît ni sur le dépôt ni sur ton profil GitHub,
     Certum peut refuser : à toi de voir si tu l'y fais figurer.
   - Délai : Certum n'en annonce pas ; compte quelques jours à deux semaines.
3. **Activer SimplySign** quand Certum t'écrit (un e-mail de bienvenue, un e-mail avec un lien
   d'activation, un e-mail avec un code) :
   - installe l'application **SimplySign** sur ton téléphone (iPhone ou Android) ;
   - ouvre le lien d'activation sur ton PC et tape le code reçu : **un QR code s'affiche** ;
   - **avant de le scanner, garde-en une copie** : capture d'écran rangée dans ton gestionnaire de mots
     de passe. Ce QR code contient le secret qui permettra à GitHub de signer. À notre connaissance il
     n'est affiché qu'une fois ;
   - scanne-le avec l'application SimplySign : elle te donne maintenant des codes à 6 chiffres.
   - **Ne l'envoie à personne**, ni par message, ni à un agent : c'est l'équivalent de ta clé.
4. **Lire le texte du QR code** : la plupart des gestionnaires de mots de passe savent lire un QR code
   de « code à usage unique » et montrent ensuite un texte qui commence par `otpauth://`. C'est ce
   texte qu'il faut pour l'étape suivante. Si tu n'y arrives pas, dis-le : on le fera ensemble sur ton
   PC, sans que le secret en sorte.
5. **Créer les deux secrets et les deux variables dans GitHub**, dépôt `VALXXII/browser` (les noms sont
   dans le tableau de la partie 4) :
   - `CERTUM_SIMPLYSIGN_ID` et `CERTUM_SIMPLYSIGN_TOTP_SECRET` dans l'environnement `release` ;
   - la variable `RELEASE_SIGNER_NAME` : le nom exact du certificat, tel que Certum l'a émis
     (il commence par « Open Source Developer, ») ;
   - la variable `RELEASE_SIGNING` : ne la crée pas encore.
6. **Prévenir Chief.** On fusionne alors la chaîne, tu mets `RELEASE_SIGNING` sur `rehearsal`, et on
   regarde une ou deux publications : GitHub essaie de signer, la version publiée reste comme
   aujourd'hui. On corrige la connexion automatique si besoin.
7. **Essayer une version signée sur un PC où le Contrôle intelligent est actif** (le fichier signé de la
   répétition). C'est la seule preuve qui compte.
8. **Mettre `RELEASE_SIGNING` sur `on`.** À partir de là chaque version publiée est signée.

À savoir : le certificat dure environ un an (459 jours au plus) ; son renouvellement repasse par les
étapes 2 à 5 si Certum redonne un QR code. Si le navigateur devenait payant, ce certificat ne
conviendrait plus (Certum le révoque) : il faudrait passer à une autre option du tableau.

## 7. Que dire aux utilisateurs bloqués maintenant

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

## 8. Sources

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
- Certum, activation de SimplySign et installation des applications (sans date). https://support.certum.eu/en/how-to-activate-access-to-simply-sign-application/ et https://support.certum.eu/en/installation-of-the-simplysign-application/
- Projets publics qui signent avec Certum depuis GitHub (méthode non officielle, sources secondaires). https://github.com/jay0lee/certum-cloud-code-sign et https://www.devas.life/how-to-automate-signing-your-windows-app-with-certum/
- Presse, pour dater la réactivation du Contrôle intelligent (source secondaire). https://www.neowin.net/news/microsoft-removes-mandatory-clean-install-requirement-for-a-windows-11-exclusive-feature/

Non vérifié ce jour : la France dans la liste des pays du compte Store (page muette) ;
l'appartenance de Certum au programme de Microsoft (connue, non relue) ; le délai réel de
Certum ; l'accord de Certum pour une signature sans personne devant l'écran ; le comportement
réel du Contrôle intelligent face à un fichier signé par un certificat tout neuf (des
témoignages non vérifiés parlent de blocages malgré la signature).
