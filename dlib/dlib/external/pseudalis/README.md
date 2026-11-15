# Pseudalis

> **Fork macOS** (TortueTech) : Pseudalis pour les Mac Apple Silicon seulement. Son application web parle
> anglais et français seulement ; les modèles de reconnaissance de dlib lisent toujours les autres langues
> citées plus bas. Le reste de ce document décrit l'original, pour Windows.

Pseudalis protège les données personnelles des documents sur un ordinateur Windows, sur cet ordinateur seul.
Un service anonymise les fichiers et les dossiers qu'il reçoit par un lecteur, par des commandes de
l'Explorateur, par une application web et par une interface locale, et garde leurs originaux chiffrés, pour
que chacun revienne à la demande depuis n'importe quelle copie de son fichier anonymisé, par les mêmes
commandes, la même application et la même interface. Il repose sur [`dlib/privacy`](../../privacy.h) et sur
les modèles de reconnaissance entraînés pour l'exemple
[`slm_document_anonymizer_ex`](../../../examples/slm_document_anonymizer_ex.cpp), que ses moteurs embarquent.

Pseudalis Community, l'édition publiée ici, est écrite par Aldric Pierrain, Copyright (C) 2026
Cydral Technology, et distribuée sous la Boost Software License 1.0
([`resources/license.txt`](resources/license.txt)). Elle n'installe aucun pilote : son lecteur est un dossier
de son dossier de données, que le service surveille et que Windows montre sous sa lettre. L'installateur de
chaque version se trouve dans [`dist/pseudalis`](../../../../dist/pseudalis), où chacun le télécharge.

## Ce qu'il fait

Chaque valeur personnelle d'un document (noms, adresses, numéros de téléphone, adresses e-mail,
identifiants, coordonnées bancaires, dates et les autres types de `dlib/privacy`) est remplacée par une
balise comme `[[PERSON:Xoydoeh Nuggwdfsorn#EKR]]`, qui porte la valeur chiffrée par FF1 sur AES-256 et un
contrôle. La clé de l'ordinateur seule restaure le document, ou tout texte qui en dérive. Les fichiers Word,
Excel, PowerPoint, PDF, texte, CSV, Markdown, HTML, XML, JSON et YAML gardent leur mise en page ; un PDF
garde ses pages, son texte redessiné en place.

Les endroits qu'une balise abîmerait reçoivent la valeur sous sa forme protégée sans la balise, son
substitut : `Xoydoeh Nuggwdfsorn` pour `Jean Dupont`, une date décalée, écrite dans la forme de l'originale.
Ce sont le nom du fichier (`Contrat Jean Dupont.docx` devient `Contrat Xoydoeh Nuggwdfsorn.docx`), les noms
des entrées et des dossiers d'une archive, et les propriétés que montrent les fenêtres de propriétés :
titre, sujet, auteurs, mots-clés, description, catégorie, société, responsable et propriétés personnalisées
de Word, Excel et PowerPoint, titre, auteur, sujet, mots-clés et créateur d'un PDF. Un substitut ne contient
aucun caractère que Windows refuse dans un nom. La base de données enregistre chaque substitut avec sa
balise, jamais avec sa valeur en clair ; une restauration rend le nom original et les propriétés
originales, y compris celles d'une copie modifiée depuis, tant que les substituts restent en place.

### Sans balises

Les valeurs peuvent aussi s'écrire sans leurs balises, par leurs seuls substituts, comme le sont déjà les
noms : le fichier se lit alors comme un document ordinaire, pour une consigne envoyée à un assistant, des
données de test ou d'entraînement, ou un fichier envoyé à l'extérieur, mais plus rien n'y distingue une
valeur de son substitut, et seul son enregistrement rend ses valeurs. Le réglage « Écrire les balises par
défaut », en haut de la carte « Réglages » de l'application web (`tags`, activé par défaut, gardé avec les
autres réglages quand le service redémarre), décide pour le lecteur et pour les requêtes de l'interface qui
ne le précisent pas (`tags=false` ou `true`) ; les cases des cartes « Protéger des fichiers » et « Protéger
un texte » partent de lui, et chacune ne s'applique qu'à ses fichiers ou à son texte, ce qui essaie
l'interface en direct ; les commandes de l'Explorateur proposent les deux.

- L'enregistrement d'un fichier protégé sans balises garde son original, comme tout enregistrement, et les
  substituts de son texte avec les balises qu'ils remplacent, jamais ses valeurs en clair : ses copies
  rendent l'original, et une copie modifiée depuis, reconnue par son identifiant d'objet NTFS, ses valeurs
  à partir de ces substituts, sans ses images. La liste des originaux conservés indique quels fichiers ont
  été protégés sans balises.
- Une fois son original oublié, à la demande d'un utilisateur, au terme de sa durée ou pour faire de la
  place, ses valeurs ne peuvent plus revenir : « Restaurer » refuse le fichier (HTTP 410, code
  `tagless_forgotten`) et indique la date de l'oubli, et « Oublier » le signale avant d'oublier. Un fichier
  protégé sans balises et sans enregistrement (« Conserver les originaux » décoché) ne revient jamais, ce
  que dit son rapport (`"restorable": false`).
- Un texte protégé sans balises dans l'application web garde la liste de ses substituts dans la page, qui
  rend le texte, ou la réponse d'un assistant qui les cite, tant que la page reste ouverte ; l'interface
  donne cette liste dans `substitutions`, que `/v1/text/restore` reprend.
- Un fichier protégé qui est protégé à nouveau, par une version plus récente qui y trouve plus de valeurs
  par exemple, garde les valeurs protégées auparavant : avec balises, telles quelles, y compris les
  substituts que garde son enregistrement, dont hérite son nouvel enregistrement ; sans balises, ses
  balises cèdent la place aux substituts de leurs valeurs, que garde son nouvel enregistrement. Un fichier
  du lecteur protégé sans balises et modifié sur place est protégé à nouveau de la même façon. Sa
  restauration rend les valeurs des deux protections, avec ou sans balises : l'original de chaque
  enregistrement de la chaîne qui le garde encore, les valeurs des balises et des substituts hérités des
  autres, sans aucun avertissement.

### Les types de fichiers

Un fichier est traité quand son extension nomme l'un des types de Pseudalis et que son contenu est de ce
type, ou, avec toute autre extension ou sans extension, quand son contenu est du texte de bout en bout :

| Type | Extensions |
|---|---|
| Word, Excel, PowerPoint | `.docx` `.docm` `.dotx` `.dotm` `.xlsx` `.xlsm` `.xltx` `.xltm` `.pptx` `.pptm` `.potx` `.potm` `.ppsx` `.ppsm` |
| PDF | `.pdf` |
| Textes | `.txt` `.text` `.log` `.md` `.markdown` `.rst` `.csv` `.tsv` `.tab` `.json` `.jsonl` `.ndjson` `.xml` `.html` `.htm` `.xhtml` `.svg` `.yaml` `.yml` `.toml` `.ini` `.cfg` `.conf` `.properties` `.sql` `.tex` `.srt` `.vtt` |
| Images | `.png` `.jpg` `.jpeg` `.jpe` `.jfif` `.gif` `.bmp` `.tif` `.tiff` `.webp` |
| Archives | `.zip` `.tar` `.tgz` `.tar.gz` |

Un fichier de toute autre extension, ou sans extension, `journal.log2` ou `README` par exemple, est traité
comme un texte quand son contenu est du texte de bout en bout, ce que le moteur vérifie avec la détection de
dlib (`dlib/data_io`) : aucune signature binaire (images, sons, vidéos, programmes, archives, PDF), la
proportion de caractères imprimables et l'entropie d'un texte sur son début et sur tout son contenu, chaque
caractère décodé, en UTF-8, UTF-16 ou Windows-1252, sans autre caractère de contrôle que la tabulation, les
fins de ligne et le saut de page, et sans partie encodée qui soustrairait des valeurs au détecteur. Une
partie encodée est un bloc de chiffres base64, base64url ou hexadécimaux (1024 caractères d'affilée, ou
16 suites d'affilée de 60 ou plus, les lignes d'un bloc, dans une chaîne de JSON ou d'un programme aussi,
ce que ne forment ni une ligne de tirets ni un chemin), des données ou du JSON en base64 (les jetons
d'OpenID Connect, `eyJ...`), des octets écrits en hexadécimal, un vidage ou un tableau (64 d'affilée, ou
4 lignes d'affilée de 8 ou plus, avec des lettres ou des signes parmi eux, `4a 65`, `0x4a`, `\x4a`, les
groupes de 4 ou 8 chiffres de xxd ou od comptant dans les lignes qui commencent par un décalage), ou un
format qui écrit ses valeurs encodées : un message avec des parties en base64, quoted-printable ou
uuencode, ou avec des mots encodés dans ses en-têtes (`=?UTF-8?B?...?=`), un document RTF, une vCard ou un
iCalendar, du LDIF avec des valeurs en base64 (`cn:: ...`), un bloc en armure PEM ou OpenPGP
(`-----BEGIN ...-----` et son corps), dans une chaîne aussi, un bloc d'uuencode ou d'ASCII85, et, par sa
première ligne, un programme PostScript ou un export du registre de Windows. L'interface refuse un tel texte
avec le code `encoded_content`. Sans extension, une image, un document Office, une archive ou un PDF est
aussi reconnu par sa signature. Un fichier d'une extension du tableau garde ses règles : son contenu doit
être du type de son extension, et un texte est lu tel quel. Pour un fichier d'une extension qui n'appartient
à aucun type, le lecteur et les commandes lisent d'abord les 64 premiers Ko, et laissent un fichier dont le
début n'est pas un texte sans lire le reste, sauf si le lecteur contient un fichier protégé de cette
taille, qui peut être un fichier renommé.

Tout autre fichier est d'un autre type, et c'est aussi le cas d'un fichier dont le contenu n'est pas du
type que donne son nom : un document OpenDocument, un document d'un ancien format Office, un programme, un
texte nommé `.docx`. Son contenu sortirait à moitié protégé, compressé ou encodé tel quel, donc Pseudalis ne
l'écrit jamais dans un endroit anonymisé : le lecteur le déplace ailleurs, les commandes le laissent tel
quel, une archive ou une copie de dossier anonymisée l'écarte, et l'interface le refuse avec le code
`unsupported` ou `kind_mismatch`. Une archive ZIP nommée `.zip` est une archive quel que soit son contenu,
y compris un document Office renommé.

Un dossier passe en entier, sous-dossiers et leurs fichiers compris, sur le lecteur, par les commandes de
l'Explorateur, et par l'application web, qui l'envoie sous forme d'archive ZIP et rend l'archive
anonymisée. Les archives et les dossiers gardent leur arborescence : leurs documents sont anonymisés, leurs
images deviennent des fichiers texte, les noms de leurs fichiers et de leurs dossiers reçoivent des
substituts, les mêmes dans toutes les entrées qui partagent un dossier, et leurs fichiers d'un autre type
sont écartés et listés. Une restauration garde tel quel chaque fichier qu'elle ne peut pas lire, sous son
nom restauré, et lit les balises d'un texte sous n'importe quel nom.

### Le lecteur

L'installateur donne au lecteur une lettre choisie parmi les lettres libres, `A:` quand elle est libre.
C'est le dossier `drive` du dossier de données, que le service montre sous sa lettre pour chaque session de
l'ordinateur, comme le ferait la commande `subst`, et qu'il surveille : il fonctionne comme n'importe quel
dossier, depuis l'Explorateur et depuis l'invite de commandes (`dir`, `copy`, `xcopy`, `move`, `del`,
`ren`, `md`, `rd /s`, `type`, n'importe quel éditeur), à une différence près : un fichier est anonymisé, nom
compris, une fois que le programme qui l'écrit l'a fermé et qu'il est resté deux secondes sans changer. Le
fichier anonymisé prend sa place, l'original part dans le coffre chiffré, et le fichier écrit est supprimé.

- Un document garde son type et sa mise en page, ses valeurs remplacées par des balises dans son contenu,
  ou par leurs seuls substituts quand le réglage « Écrire les balises par défaut » est désactivé, et par
  des substituts dans son nom et ses propriétés.
- Une image devient `<name>.txt`, son repère `[Image@<id> : <description>]`, ou `[Image@<id>]` quand aucune
  description n'est disponible, et l'image part dans le coffre.
- Une archive ZIP, TAR ou TAR.GZ garde son type, les valeurs de son nom remplacées ; ses documents sont
  anonymisés, ses images deviennent des fichiers texte, les noms de ses entrées et de ses dossiers
  reçoivent des substituts, et ses autres entrées sont écartées, ce que dit son rapport.
- Un dossier qui arrive prend son nom, ses valeurs remplacées, une fois que rien n'a changé à l'intérieur
  pendant six secondes : une copie en cours garde ses dossiers.
- Un fichier d'un autre type, ou dont le contenu ne peut pas être lu, un document abîmé par exemple, quitte
  le lecteur pour le dossier des fichiers non traités de l'utilisateur qui l'a écrit (`refused\<user>` dans
  le dossier de données, que seuls cet utilisateur, SYSTEM et les administrateurs lisent), à la place qu'il
  avait dans l'arborescence du lecteur, et l'icône le signale ; son menu ouvre ce dossier.
- Un fichier qu'un programme tient encore, un document ouvert dans son éditeur par exemple, attend que le
  programme le ferme : un document modifié sur le lecteur est anonymisé à nouveau quand son éditeur le
  ferme, et sa version précédente quitte la liste des fichiers du lecteur. Pendant que le service traite un
  fichier, aucun autre programme ne l'ouvre (violation de partage), quelques secondes pour la plupart des
  documents. Les fichiers de verrou et les fichiers temporaires des éditeurs, dont les noms commencent par
  `~` ou finissent par `.tmp`, sont laissés tels quels.
- Supprimer un fichier, un dossier ou toute l'arborescence les retire du lecteur. L'original d'un fichier
  supprimé ou remplacé par une nouvelle version reste restaurable depuis les copies du fichier pendant
  45 jours (le réglage `removed_days`, 0 pour aucune limite de durée) une fois que la dernière copie du
  lecteur l'a quitté, puis quitte le coffre avec les images qu'aucun autre fichier ne montre. Un fichier
  déplacé, renommé ou copié à l'intérieur du lecteur garde son original, que toutes ses copies partagent.
- Quelle que soit leur durée, les originaux conservés, ceux du lecteur et de toutes les autres sources,
  restent dans la limite de 10 Go (le réglage `history_mb`, 10240 Mo, jamais en dessous de `max_file_mb`) :
  les premiers originaux conservés sont oubliés pour faire de la place, les plus anciens d'abord, avant
  qu'un nouveau soit écrit, toutes les heures et aussitôt quand les réglages abaissent les jours ou
  l'espace. Un nouvel original laisse aussi 256 Mo libres sur le disque du dossier de données : les plus
  anciens lui font de la place quand les oublier libère cette place, et restent quand d'autres fichiers
  remplissent le disque. Une installation mise à jour depuis la version 1.0.1 garde chaque original : ses
  réglages, sans `history_mb`, prennent l'espace le plus grand (1048576 Mo) et les 30 jours de sa valeur par
  défaut deviennent 45, jusqu'à ce qu'un administrateur les règle ; un `settings.json` abîmé les garde de la
  même façon.
- Un fichier extrait sur le lecteur d'une archive anonymisée que le service a enregistrée reste tel quel,
  et la liste des originaux conservés le montre sous son nom original, son original étant l'entrée de
  l'archive originale, que le coffre garde à part : l'archive et chaque fichier qui en est extrait sont
  restaurés et oubliés séparément. Un fichier extrait ailleurs est restauré depuis l'original de l'archive
  tant que le service le garde.
- Un fichier anonymisé dont l'original a été oublié, ou a quitté le coffre au terme de sa durée, reste tel
  quel quand il revient sur le lecteur, sans enregistrement : il n'est pas anonymisé à nouveau, ce qui
  garderait le fichier anonymisé lui-même comme nouvel original. Sa restauration remet les valeurs de ses
  balises, comme pour les fichiers extraits d'une telle archive. Un document modifié sur le lecteur et
  anonymisé à nouveau, ou écrit ailleurs à partir de valeurs anonymisées puis copié sur le lecteur, garde
  son nom en clair dans la liste des originaux, et son original revient avec les valeurs de ses balises,
  sans les repères de ses images.

Le lecteur ne rend jamais un original : les originaux reviennent par « Restaurer » dans l'Explorateur,
l'application web et l'interface, depuis n'importe quelle copie du fichier anonymisé.

### Les commandes de l'Explorateur

Un clic droit sur un fichier de l'une des extensions ci-dessus, ou sur un dossier, affiche, sous « Afficher
d'autres options » sur Windows 11, un menu Pseudalis de six commandes, que `pseudalis.exe` exécute par
l'intermédiaire du service ; un texte d'une autre extension passe par les commandes de son dossier, le
lecteur, l'application web ou l'interface, le menu restant absent des programmes, des sons et des vidéos :

| Commande | Sur un fichier | Sur un dossier |
|---|---|---|
| Anonymiser avec les balises, Anonymiser sans les balises | le fichier est remplacé par sa version anonymisée ; une image par son fichier texte | chaque fichier, puis chaque dossier sous son nom anonymisé |
| Anonymiser en copie, avec ou sans les balises | `name (anonymized).ext` est écrit à côté du fichier | `name (anonymized)` reçoit l'arborescence anonymisée |
| Restaurer l'original | l'original prend la place du fichier, sous son nom original | chaque fichier et chaque dossier reprend son original |
| Restaurer en copie | `<original name> (restored).ext` est écrit à côté du fichier | `<original name> (restored)` reçoit l'arborescence restaurée |

Les commandes sans balises écrivent les valeurs par leurs seuls substituts (voir « Sans balises » plus
haut). Une sélection de 100 fichiers ou dossiers au plus reçoit les commandes, que l'Explorateur lance une
fois pour chacun : elles passent à tour de rôle, deux à la fois, et l'icône notifie ensemble les résultats
qui arrivent en moins d'une seconde et demie.

Les suffixes suivent la langue de l'installation, et ` (2)` est ajouté à un nom déjà pris. Dans un dossier,
un fichier d'un autre type reste tel quel à sa place, hors d'une copie anonymisée, et tel quel dans une
copie restaurée, qui prend aussi les fichiers qui n'ont rien à restaurer ; un fichier déjà anonymisé est
compté comme tel. Les liens symboliques et les jonctions sont ignorés, les fichiers cachés du système et les
fichiers de verrou des éditeurs (`~$...`) restent tels quels, et un dossier s'arrête à 100 000 fichiers ou
64 niveaux. Une notification indique les fichiers traités, ceux déjà anonymisés, ceux d'un autre type,
nommés, et le premier échec. L'original d'un fichier anonymisé est retrouvé par le SHA-256 de son contenu,
et par l'identifiant d'objet NTFS du fichier, que la commande enregistre avec son emplacement ; un fichier
modifié depuis son anonymisation retrouve ses valeurs par ses balises, et son nom et ses propriétés par les
substituts que la base de données a enregistrés. Un fichier dont l'original a été oublié, depuis
l'application web, au terme de sa durée une fois que son fichier a quitté le lecteur ou pour faire de la
place à des originaux plus récents, et un fichier écrit ailleurs à partir de valeurs anonymisées, un
document qu'un modèle de langage a écrit à partir d'une consigne anonymisée par exemple, sont restaurés par
leurs balises de la même façon, tant que la clé demeure : « Restaurer » indique la date de l'oubli. Une
telle restauration ne peut pas rendre une image : le repère d'une image quitte le document, ses passages
dans Word, sa zone de texte grise sur une diapositive, son texte sur la zone grise d'un PDF, qui reste, et
le fichier texte d'une image quitte une archive ; « Restaurer » les compte. Le fichier texte d'une image
seule rend son image tant que le coffre la garde, et il est refusé une fois qu'elle a été oubliée
(HTTP 410, code `picture_forgotten`). Un fichier enregistré par un autre programme pendant qu'une commande
le traite reste tel quel, et la commande le signale. Un fichier du lecteur est déjà anonymisé :
« Restaurer » demande où enregistrer son original, hors du lecteur, qui l'anonymiserait à nouveau, ainsi
que le dossier où vont les originaux d'un dossier du lecteur. Le même programme se lance depuis une invite
de commandes, `pseudalis protect [--copy] [--no-tags] <file or folder>...` et
`pseudalis restore [--copy] <file or folder>...`, ses résultats s'affichant là (`start /wait pseudalis ...`
les attend).

### L'application web et l'interface

L'icône ouvre l'application web du service, `https://localhost:8444` : les fichiers qu'on y dépose sont
anonymisés ou restaurés puis téléchargés en retour, un dossier déposé ou choisi là sous forme d'archive ZIP
de son arborescence, les textes sont anonymisés ou restaurés sur place, leurs balises surlignées et leurs
valeurs restaurées soulignées dans la couleur de leur type, ou analysés, les valeurs trouvées listées
dessous, et, ouverte depuis l'icône, elle liste les originaux conservés (télécharger, oublier), exporte et
importe la clé, et change les réglages et les termes imposés. Ses fichiers et ses textes reçoivent ou non
leurs balises selon la case de leur carte, qui part du réglage « Écrire les balises par défaut », et elle
pose ses questions et donne ses informations dans une fenêtre à elle, par-dessus la page, la touche Échap
ou un clic à côté annulant. L'interface qu'elle utilise est décrite avec Swagger UI à
`https://localhost:8444/docs/` (« Documentation de l'interface » dans le menu de l'icône), sans compte : les
opérations d'administration demandent le jeton du dossier de données dans l'en-tête `X-Pseudalis-Admin`,
que les pages ouvertes par l'icône reçoivent dans le fragment de leur adresse et utilisent d'elles-mêmes.

| Route | Rôle |
|---|---|
| `GET /v1/health`, `GET /v1/info` | l'état du service, de son moteur, du lecteur et d'Albireo |
| `POST /v1/anonymize?name=&record=&tags=&format=` | anonymise le fichier du corps, sans balises avec `tags=false` ; la réponse est le fichier, ou du JSON avec `format=json` |
| `POST /v1/restore?name=&object_id=&format=` | restaure le fichier du corps : son original, son image ou ses balises |
| `GET /v1/lookup?sha256=&object_id=` | si un fichier est un fichier anonymisé de cet ordinateur |
| `POST /v1/records/{id}/location` | enregistre où un fichier anonymisé a été écrit (jeton) |
| `POST /v1/text/anonymize`, `/restore`, `/detect` | la même protection sur un texte, `{"text": ...}`, `"tags": false` sans balises, dont `/restore` reprend les `substitutions` ; les emplacements des balises écrites (`tags`) ou des valeurs restaurées (`restored`) reviennent avec lui |
| `POST /v1/names/anonymize`, `/restore` | un nom seul, celui d'un dossier, `{"name": ...}`, ses substituts enregistrés |
| `GET /v1/drive`, `GET /v1/events?since=` | le lecteur, les fichiers qu'il a protégés ou refusés (jeton pour les événements) |
| `GET /v1/albireo` | si Albireo décrit les images |
| `GET /v1/records`, `GET /v1/records/{id}/original`, `DELETE /v1/records/{id}` | les originaux conservés (jeton) |
| `GET /v1/settings`, `PUT /v1/settings`, `POST /v1/restart` | les réglages et le redémarrage (jeton) |
| `GET /v1/terms`, `PUT /v1/terms` | les termes imposés (jeton) |
| `GET /v1/updates`, `POST /v1/updates/check`, `POST /v1/updates/apply` | les mises à jour |
| `POST /v1/key/export`, `POST /v1/key/import` | la clé, protégée par une phrase secrète (jeton) |
| `GET /v1/logs` | les dernières lignes du journal (jeton) |

### L'icône

L'icône de la zone de notification suit le service : son info-bulle donne la version, le lecteur et le
nombre d'originaux conservés, et elle notifie les fichiers que le lecteur a anonymisés ou n'a pas traités,
les résultats des commandes de l'Explorateur, les mises à jour et un service qui cesse de répondre. Elle
signale à l'Explorateur le lecteur qui apparaît et disparaît. Son menu ouvre Pseudalis (l'application web),
le lecteur et le dossier des fichiers non traités de son utilisateur, cherche les mises à jour et les
installe, gère le service, ouvre la documentation de l'interface et la fenêtre « À propos ». « Gestion du
service » redémarre le service, et l'arrête pendant qu'il tourne ou le démarre pendant qu'il est arrêté :
un service arrêté là reste arrêté, sans l'avertissement de l'icône, jusqu'au prochain démarrage de
l'ordinateur, son démarrage avec le système restant inchangé, ou jusqu'à ce qu'un utilisateur le démarre.

### Termes imposés

Les administrateurs donnent jusqu'à 100 termes que Pseudalis remplace dans chaque fichier, texte et nom
avant de lire quoi que ce soit d'autre : le nom d'un client, d'un projet, d'un contrat ou d'une
organisation que le détecteur ne connaît pas, chacun avec la balise de son type (ORG, PERSON, LOCATION,
ADDRESS, ID, EMAIL, PHONE ou URL) et, sur demande, sa casse. Un terme est trouvé en mots entiers, ses mots et
ses nombres dans le même ordre, séparés par deux caractères au plus d'une même ligne (« Projet Alpha »,
« PROJET-ALPHA ») ; sans respect de la casse, sa casse et ses accents sont ignorés. Ses occurrences
l'emportent sur toute autre valeur trouvée : un nom ou une organisation lu par-dessus un terme garde ses
parties hors de lui, tandis qu'un terme à l'intérieur d'une adresse e-mail, d'une adresse web, d'une date ou
d'un nombre reste protégé avec cette valeur. L'application web modifie la liste dans sa carte « Termes
imposés », l'interface par `GET` et `PUT /v1/terms` ; le service la garde dans sa base de données, que
seuls SYSTEM et les administrateurs lisent, et le moteur applique aussitôt une nouvelle liste, aux fichiers
traités ensuite.

### Les images et Albireo

Quand Albireo, le service d'inférence local, est installé sur le même ordinateur et que l'un de ses modèles
lit les images, Pseudalis le trouve par son interface (son dossier de données, son port, son autorité
locale) et lui demande de décrire chaque image, dans la langue de l'installation, sans nommer personne ni
recopier aucun nom, adresse, numéro ou identifiant que l'image montre. Une description peut tout de même
nommer ce que montre l'image : le moteur la lit avec le détecteur avant de l'écrire, avec ce que le texte du
document a appris en plus au détecteur, un nom que la description répète seul par exemple, ses valeurs
remplacées par des balises et comptées dans le rapport avec celles du fichier, pour qu'elle ne ramène
aucune donnée personnelle dans le fichier anonymisé ; c'est seulement ensuite qu'elle est coupée à
1000 octets, hors de ses balises. Une image seule devient `<name>.txt` contenant
`[Image@<id> : <description>]`, et une image à l'intérieur d'un document donne le même texte à sa place (un
passage de Word, une zone de texte de PowerPoint à l'emplacement de l'image, la légende d'une zone grise
dans un PDF). L'identifiant est une empreinte de l'image calculée avec la clé de l'installation, qui ne dit
rien de l'image à qui ne détient pas la clé et ne relie aucune image d'une installation à une autre ; il est
enregistré dans la base de données avec l'image et sa description dans le coffre, toutes deux chiffrées,
pour que restaurer le fichier texte rende l'image et qu'une image rencontrée de nouveau garde sa
description, protégée à nouveau chaque fois qu'elle est écrite. Le réglage `albireo` désactive les
descriptions, y compris celles qui sont conservées.

### Langues

L'installateur, l'icône, les commandes de l'Explorateur et l'application web parlent anglais, français,
allemand, italien et espagnol, et anglais pour toute autre langue. Ils suivent la langue du système :
l'icône et les commandes de chaque utilisateur prennent la langue du Windows de cet utilisateur,
l'application web la première langue du navigateur qu'elle parle (son menu en choisit une autre, gardée par
le navigateur), et l'installateur la langue de Windows, que sa première page permet de changer. Le réglage
`language`, `auto` par défaut, fixe à la place une seule langue pour tous les utilisateurs ; une langue
choisie dans l'installateur, autre que celle de Windows, devient ce réglage. Les erreurs du service
parviennent à l'application web et aux fenêtres sous forme de codes, écrits dans leur langue.

## Programmes et données

| Fichier | Rôle |
|---|---|
| `pseudalis-service.exe` | le service (LocalSystem) : le moteur, le lecteur, le coffre, la base de données, l'interface sur `127.0.0.1` |
| `pseudalis-engine.dll`, `pseudalis-engine-avx2.dll` | le moteur, pour tout processeur x64 et pour ceux qui ont AVX2, choisi au démarrage du service |
| `pseudalis-tray.exe` | l'icône, lancée à l'ouverture de session de chaque utilisateur |
| `pseudalis.exe` | les commandes de l'Explorateur |
| `pseudalis-setup.exe` | l'installateur, gardé dans le dossier des programmes pour servir de programme de désinstallation |
| `web\` | l'application web et la documentation de l'interface |
| `licenses\` | les licences des composants |

Les données se trouvent dans `%ProgramData%\Pseudalis`, ou dans le dossier choisi à l'installation,
enregistré dans `HKLM\Software\Pseudalis` : `settings.json`, `admin.token`, `keys\master.key`, `vault\`,
`db\pseudalis.db`, `certificates\`, `logs\`, `updates\`, le dossier du lecteur, `drive\`, et le dossier des
fichiers non traités, `refused\`.

## Sécurité

- La clé maîtresse, 32 octets aléatoires, est chiffrée avec DPAPI pour le compte du service (LocalSystem)
  dans `keys\master.key`, dont les permissions n'admettent que SYSTEM et les administrateurs. HKDF-SHA-256
  en dérive la clé de la pseudonymisation et les deux clés du coffre.
- Chaque original, chaque image et chaque description d'une image par Albireo est gardé dans le coffre,
  chiffré avec AES-256-CTR et authentifié avec HMAC-SHA-256 ; la base de données contient les noms, les
  tailles, les empreintes et les emplacements des fichiers, jamais leur contenu ni une description : celles
  qu'une base de données d'une version antérieure gardait en clair sont effacées quand le service l'ouvre.
- Les programmes et les moteurs lient statiquement la bibliothèque d'exécution du compilateur et
  n'importent que les bibliothèques de Windows, ce que le build vérifie. Construits avec Visual C++, le
  service, l'icône, les commandes et l'installateur ne prennent les bibliothèques qu'ils importent que dans
  System32 (`/DEPENDENTLOADFLAG`), jamais dans le dossier du programme, un dossier de téléchargements pour
  l'installateur par exemple, et font leurs chargements ultérieurs dans System32, et dans son propre dossier
  pour le service (`SetDefaultDllDirectories()`). Le service ne charge que le moteur de son propre build,
  par son chemin complet, dans le dossier des programmes, dont les permissions n'admettent que SYSTEM et les
  administrateurs pour toute modification : une bibliothèque du même nom qu'une autre installation a placée
  ailleurs n'est jamais chargée.
- La clé peut être exportée, protégée par une phrase secrète de 12 caractères ou plus (PBKDF2-SHA-256 avec
  600 000 itérations, AES-256-CTR et HMAC-SHA-256), et importée dans une nouvelle installation qui ne garde
  encore aucun original. Sans sa clé, un document anonymisé ne peut être restauré nulle part : l'export est
  la sauvegarde de l'installation.
- L'interface n'écoute que sur l'adresse de bouclage, en HTTPS avec un certificat de l'autorité locale
  « Pseudalis Local Authority », restreinte par des contraintes de noms aux noms et adresses locaux, dont la
  clé n'est jamais écrite et dont la clé de serveur est chiffrée avec DPAPI. Les requêtes dont le `Host`
  n'est pas local, et les requêtes d'une autre origine, sont refusées, pour qu'aucune page web ne lise ni ne
  change quoi que ce soit par l'intermédiaire du navigateur.
- Le service sert plusieurs sessions et plusieurs utilisateurs à la fois : chaque requête est traitée sur
  un thread à elle, les fichiers du lecteur sur quatre threads au plus qui leur sont propres, le moteur fait
  tourner ses modèles de reconnaissance pour plusieurs fichiers à la fois, et les verrous du service sont
  pris dans un ordre unique, sans attente sur le réseau ni sur un client tant qu'ils sont tenus.
  Les fichiers et les textes de l'interface traités en même temps sont bornés par les processeurs et la
  mémoire (la moitié des processeurs, de 2 à 8) : une requête qui ne trouve aucune place libre en une
  minute reçoit 503 et `Retry-After`. Un corps plus grand que ce que sa route accepte est refusé avant
  d'être lu (413).
- Une archive ou un document Office peut se décompresser à 64 fois sa taille au plus, de 16 Mo à 2 Go,
  tous niveaux confondus, et chaque partie d'un document Office à 32 fois le document au plus, à partir de
  32 Mo : un petit fichier qui contiendrait des gigaoctets de texte est refusé. Un texte est analysé par
  morceaux de 64 Ko, pour que sa mémoire reste bornée quelle que soit sa longueur.
- Le modèle de confiance est l'ordinateur : tous les utilisateurs de l'ordinateur partagent sa clé, ses
  originaux et son administration. Tous les programmes des utilisateurs atteignent l'interface, et le jeton
  d'administration se trouve dans le dossier de données, lisible par les utilisateurs interactifs : un
  utilisateur de l'ordinateur peut restaurer n'importe quel fichier anonymisé, lire les enregistrements et
  exporter la clé. Des personnes qui ne doivent pas voir les documents les unes des autres ont besoin
  chacune d'une installation, sur des ordinateurs distincts.
- Le dossier du lecteur donne ses fichiers aux utilisateurs interactifs, à SYSTEM et aux administrateurs ;
  les utilisateurs y lisent, y écrivent et y suppriment, et n'y changent aucune permission, propriétaires
  des fichiers compris. Le dossier des fichiers non traités d'un utilisateur, et chaque fichier qui y est
  déplacé, n'admettent que cet utilisateur, SYSTEM et les administrateurs.
- Le service ouvre lui-même chaque fichier du lecteur, sans passer par aucun lien, vérifie où se trouve son
  handle, et le supprime ou le déplace par ce handle ; il ne suit ni ne traite un lien, une jonction ou un
  point de montage placé dans le lecteur, pour qu'aucun utilisateur ne l'entraîne ailleurs avec ses droits.
- L'icône et les commandes ne joignent le service que sur 127.0.0.1, et ouvrent ses pages à
  `https://127.0.0.1:8444` : `localhost` peut se résoudre d'abord en `::1`, où un autre compte pourrait
  écouter.
- Albireo ne décrit les images que si son dossier de données est celui que son installateur a enregistré
  dans `HKLM\Software\Albireo`, et si ce dossier, ses certificats, son autorité et ses réglages
  appartiennent à SYSTEM ou aux administrateurs : n'importe quel utilisateur peut créer un dossier dans
  `%ProgramData%`, qui recevrait sinon les images de tous les utilisateurs.
- Les messages, les réponses et les journaux des mises à jour ne nomment jamais les emplacements de la
  publication ; les réglages ne les nomment que lorsqu'ils diffèrent de ceux du build.

## Choix faits à partir de la spécification

- Le lecteur n'a besoin d'aucun pilote. Un lecteur dont les lectures et les écritures parviennent à un
  programme a besoin d'un pilote du noyau de Windows, que Windows 10 et 11 ne chargent qu'une fois signé par
  Microsoft (un certificat EV et une attestation pour chaque version). Sans pilote, le lecteur est un
  dossier que le service surveille : le fichier qu'un programme écrit se trouve en clair dans le dossier du
  lecteur, de sa dernière écriture à son traitement, quelques secondes pour la plupart des documents, sous
  les permissions du lecteur, avant que son original parte chiffré dans le coffre et que le fichier soit
  supprimé, ses secteurs libérés mais non écrasés ; BitLocker, là où il est activé, les garde chiffrés sur
  le disque. Un fichier d'un autre type ne peut pas être refusé à sa création : il quitte le lecteur pour le
  dossier des fichiers non traités de son utilisateur.
- Windows copie un fichier hors d'un lecteur, puis le supprime : un déplacement hors du lecteur ne peut pas
  se distinguer d'une copie suivie d'une suppression. Le lecteur ne rend donc jamais un original, qui
  parviendrait sinon à tout programme qui le lit ; les originaux reviennent par « Restaurer », l'application
  web ou l'interface, depuis n'importe quelle copie du fichier anonymisé, et aucun second lecteur ne les
  restaure, puisqu'il les donnerait lui aussi à n'importe quel programme. Pour la même raison, l'original
  d'un fichier qui quitte le lecteur reste dans le coffre pendant `removed_days` jours, 45 par défaut, ce
  que montrent les enregistrements de l'application web, puis s'en va, ou aussitôt quand il y est oublié ;
  avec 0 jour, il reste sans limite de durée. Parti avec son fichier, il laisserait la copie restaurable par
  ses seules balises, sans ses images ni les parties que la protection a retirées. L'espace des originaux
  est tout de même borné, par `history_mb` et par l'espace libre du disque : quand il est plein, les
  originaux les plus anciens partent en premier, quelle que soit leur durée. Une fois un original oublié,
  « Restaurer » rend son fichier protégé à partir de ses seules balises et indique la date de l'oubli : la
  base de données garde le SHA-256 et l'identifiant d'objet du fichier, jamais son contenu.
- Les noms et les propriétés reçoivent des substituts plutôt que des balises, qu'un nom ne peut pas
  contenir et qu'une fenêtre de propriétés montrerait telles quelles ; la base de données garde leurs
  balises. Un substitut est retrouvé dans un nom ou une propriété modifiés depuis tant qu'il garde sa forme,
  une suite de douze mots au plus ; un substitut de quatre caractères ou moins, une année par exemple, est
  trop courant pour y être relu, et ne revient qu'avec l'original gardé dans le coffre.
- L'enregistrement d'un fichier protégé sans balises garde les substituts de son texte avec leurs balises
  plutôt que leurs emplacements dans le fichier : les emplacements d'un document Office ou d'un PDF se
  trouvent dans ses parties compressées, qui ne rendent rien de plus que l'original que reçoivent ses copies
  exactes, tandis que les substituts, chacun un code distinct avec sa longueur, se retrouvent dans une copie
  modifiée depuis, et dans l'original d'un fichier protégé à nouveau, dont le nouvel enregistrement en
  hérite.
- Une image devient un fichier texte du même nom, `.txt` ajouté, que l'Explorateur et l'invite de commandes
  montrent comme du texte ; son image revient par « Restaurer », l'application web et l'interface.
- Un fichier d'un autre type n'est jamais écrit dans un endroit anonymisé, puisque son contenu sortirait à
  moitié protégé : le lecteur le déplace ailleurs, en gardant sa place dans l'arborescence, et une archive
  ou une copie de dossier anonymisée l'écarte et le nomme. Les types sont ceux dont Pseudalis lit tout le
  contenu.
- Sur Windows 11, les commandes de l'Explorateur se trouvent sous « Afficher d'autres options » : le premier
  menu de Windows 11 ne prend que les commandes des applications empaquetées.
- Le service écoute sur le port 8444, à côté d'Albireo sur le 8443, avec la même pile : dlib, cpp-httplib et
  Mbed TLS.
- Un PDF garde toujours ses pages : le texte est redessiné en place, et l'option d'un PDF reconstruit en
  texte seul a disparu.

## Installation

`pseudalis-setup.exe` installe les programmes, le service, l'icône, les commandes de l'Explorateur, les
raccourcis et l'entrée dans les applications installées de Windows ; relancé sur une installation, il la
met à jour et garde les données, la clé, le lecteur et les options. L'assistant demande la langue, la
licence, les dossiers, la lettre du lecteur et les options. Sans fenêtre :

```
pseudalis-setup /silent [/dir <folder>] [/data-dir <folder>] [/drive <letter>|none] [/port <port>] [/no-context-menu]
pseudalis-setup /uninstall [/silent] [/purge]
pseudalis-setup /service start|stop|restart
```

La désinstallation retire les programmes et garde la clé, les originaux, les fichiers du lecteur et les
réglages pour une installation ultérieure, sauf si l'option de l'assistant est décochée ou si `/purge` est
donné : les fichiers du lecteur partent alors avec le dossier de données, et les documents anonymisés avec
cette clé ne peuvent plus être restaurés. Un outil de déploiement qui désinstalle avant de réinstaller
(`QuietUninstallString`) ne les emporte donc jamais. Le programme de désinstallation laissé dans le dossier
des programmes est une copie du programme même qui les a installés, tenu ouvert dès son démarrage pour
qu'aucun autre compte ne le remplace entre-temps.

## Construction

### Windows

Avec Visual Studio 2022 ou ses Build Tools (charge de travail C++, CMake 3.21 ou plus récent), depuis la
racine de la copie du dépôt de la chaîne de production, `%USERPROFILE%\work\Dlib-Transformer-extensions`, la
seule que le script construit et que la publication lit (`-AnyFolder` construit une autre copie, qui n'est
pas publiée) :

```
powershell -ExecutionPolicy Bypass -File dlib\dlib\external\pseudalis\tools\build_windows.ps1
```

Le script construit chaque programme dans `build-pseudalis\bin` ; l'installateur,
`build-pseudalis\bin\pseudalis-setup.exe`, les embarque tous, avec la bibliothèque d'exécution liée
statiquement. Les moteurs compilent les modèles de l'en-tête
[`examples/slm_document_anonymizer_ex.h`](../../../examples/slm_document_anonymizer_ex.h)
(`PSEUDALIS_MODELS_HEADER`). `-Tests` construit et lance aussi les tests.

Le build vérifie, avant d'empaqueter l'installateur et une fois l'édition de liens faite, que chaque
programme et chaque moteur n'importe que les bibliothèques de Windows, trouvées dans System32, ou ses
ensembles d'API ([`tools/check_imports.cpp`](tools/check_imports.cpp)) : une bibliothèque d'exécution du
compilateur (`vcruntime`, `msvcp`, `ucrtbase`, les ensembles `api-ms-win-crt`) ou toute autre bibliothèque
arrête le build, pour que l'installateur n'embarque aucune bibliothèque d'un tiers et que les programmes
tournent sur Windows tel qu'il est installé. Chaque configuration de l'arbre de build donne aux programmes
et aux moteurs une marque de build qui leur est propre (`psd_build()` des moteurs) : le service charge son
moteur par son chemin complet, à côté de lui dans le dossier des programmes, et refuse un moteur d'une autre
version ou d'un autre build.

### Linux

Le service se construit sur Linux avec son moteur intégré et son lecteur sous forme de dossier sans lettre,
pour les tests du moteur, de l'hôte, de l'interface et du lecteur :

```
bash dlib/dlib/external/pseudalis/tools/test_linux.sh
```

### Compilation croisée

```
cmake -S dlib/dlib/external/pseudalis -B build-pseudalis-win -DCMAKE_TOOLCHAIN_FILE=dlib/dlib/external/pseudalis/cmake/mingw-w64.cmake
cmake --build build-pseudalis-win
```

Une compilation croisée ne vérifie les bibliothèques des programmes que lorsqu'elle peut lancer un
programme de Windows : `-DCMAKE_CROSSCOMPILING_EMULATOR=<wine>` lui en donne un ; sans lui, la
configuration indique que la vérification est omise.

## Publier une version

Une version est publiée sur GitHub seulement, dans [`dist/pseudalis`](../../../../dist/pseudalis) : son
installateur, `pseudalis-setup-<version>.exe`, que chacun y télécharge, et le flux de mises à jour qui le
nomme, avec sa signature (ECDSA P-256), que les copies installées y lisent. Les programmes vérifient la
signature avec la clé publique de `resources/feed-public-key.pem`, intégrée en eux. L'éditeur crée la paire
de clés une fois, sur Linux, la clé privée dans un dossier à part hors du dépôt, lisible par son seul
propriétaire, et commite la clé publique avant que l'installateur soit construit ; les copies installées
auparavant ne font confiance qu'à l'ancienne clé, et sont réinstallées à la main. Une copie de la clé privée
gardée à part est le seul moyen de publier une mise à jour que les copies installées acceptent.

```
python3 dlib/dlib/external/pseudalis/tools/publish_release.py --create-key --key ~/keys/pseudalis-feed-signing.key
```

Chaque version passe par trois étapes, chacune sur le système qui détient ce dont elle a besoin, chacune
s'arrêtant à son premier échec :

1. Linux : les sources changent et y sont testées (`tools/test_linux.sh`), la clé est vérifiée, puis les
   sources sont commitées et poussées. `PSEUDALIS_VERSION` de [`common/version.h`](common/version.h)
   augmente pour chaque version que les copies installées doivent recevoir : un flux qui annonce la version
   qu'une copie a déjà n'installe rien.
2. Windows, dans la copie de la chaîne de production, `%USERPROFILE%\work\Dlib-Transformer-extensions` :
   l'installateur est construit à partir des sources poussées, testé et installé sur place
   (`tools/build_windows.ps1 -Tests -Install`), ce qui affiche sa taille, son SHA-256 et son chemin sous WSL.
3. Linux, où se trouve la clé privée : l'installateur de cette copie, que Linux lit sous
   `/mnt/c/Users/<user>/work`, entre dans `dist/pseudalis` avec le flux écrit et signé pour lui, puis tous
   deux sont commités et poussés, et la publication se termine une fois que GitHub les sert.

```
bash dlib/dlib/external/pseudalis/tools/test_linux.sh &&
python3 dlib/dlib/external/pseudalis/tools/publish_release.py --check-key --key ~/keys/pseudalis-feed-signing.key &&
git add dlib/dlib/external/pseudalis && git commit -m "<message>" -- dlib/dlib/external/pseudalis && git push
```

```
Set-Location $env:USERPROFILE\work\Dlib-Transformer-extensions; git pull --ff-only; if ($LASTEXITCODE -eq 0) { powershell -ExecutionPolicy Bypass -File .\dlib\dlib\external\pseudalis\tools\build_windows.ps1 -Tests -Install }
```

```
git pull --ff-only &&
python3 dlib/dlib/external/pseudalis/tools/publish_release.py --key ~/keys/pseudalis-feed-signing.key --notes notes.json --push
```

Sans installateur nommé, `publish_release.py` prend celui de la copie de la chaîne de production,
`build-pseudalis\bin\pseudalis-setup.exe` sous `%USERPROFILE%\work\Dlib-Transformer-extensions`, et refuse
un installateur d'un autre dossier sauf si `--any-folder` est donné.

`publish_release.py` vérifie que l'installateur porte la version de
[`common/version.h`](common/version.h), que le flux n'en annonce aucune plus récente, que l'installateur
reste sous les limites de GitHub (il refuse 95 Mo et plus, et avertit à partir de 50 Mo) et que la clé
correspond à la clé publique des sources, copie l'installateur sous le nom `pseudalis-setup-<version>.exe`,
écrit le flux avec son nom, sa taille, son SHA-256 et les notes de chaque langue
(`{"en": "...", "fr": "...", "de": "...", "it": "...", "es": "..."}`), le signe, vérifie la signature avec la
clé publique des sources, puis met l'installateur, la signature et le flux à la place des fichiers publiés ;
les installateurs des versions antérieures quittent le dossier et restent dans l'historique du dépôt. Avec
`--push`, il vérifie d'abord que la branche n'est en retard en rien sur sa branche distante, puis commite et
pousse le dossier, et se termine une fois que GitHub sert le flux et sa signature tels qu'ils ont été écrits,
et l'installateur avec sa taille et son SHA-256, comme les copies installées les liront ; une étape qui
échoue affiche les commandes qui terminent la publication, et `--check` refait la dernière vérification. Un
échec avant l'écriture laisse les fichiers publiés tels quels, et `--dry-run` vérifie tout sans écrire. Les
services lisent le flux et sa signature sur GitHub cinq minutes après leur démarrage, puis chaque jour,
vérifient la signature, téléchargent en arrière-plan l'installateur qui se trouve à côté du flux, vérifient
sa taille et son SHA-256, et l'installent au prochain démarrage du service, ou aussitôt depuis le menu de
l'icône. Leurs messages et leurs journaux ne nomment jamais les emplacements qu'ils lisent.

## Éditions et licences

- Pseudalis Community est un logiciel libre et open source sous la Boost Software License 1.0, comme dlib
  et Dlib-Transformer-extensions ; ses composants gardent leurs licences : Mbed TLS (Apache 2.0),
  cpp-httplib (MIT), SQLite (domaine public), zlib (licence zlib), Swagger UI (Apache 2.0).
- Aucun composant de Pseudalis n'est sous une licence copyleft, puisque le lecteur n'a besoin d'aucun
  pilote : une édition sous licence propriétaire, une édition pour les entreprises vendue selon ses propres
  conditions par exemple, peut prendre chaque composant tel quel.

## Limites

- Les modèles de reconnaissance lisent le français, l'anglais, l'italien, l'espagnol et l'allemand ; un
  document dans une autre langue garde plus de valeurs en clair. Un document protégé doit être relu avant
  d'être partagé.
- Une page numérisée sans couche de texte est gardée sous forme de zone grise : aucune reconnaissance de
  caractères (OCR) n'est effectuée.
- Une description d'Albireo est lue comme l'est le texte de son document, avec les mêmes limites : une
  valeur que le détecteur manque y reste en clair. Le réglage `albireo` désactive les descriptions.
- Le lecteur accepte des fichiers de 150 Mo au plus par défaut (`max_file_mb`) : un fichier plus grand va
  dans le dossier des fichiers non traités. Un dossier envoyé par l'application web forme une seule archive,
  sous la même limite.
- Un fichier écrit sur le lecteur s'y trouve en clair jusqu'à son traitement, quelques secondes pour la
  plupart des documents (voir les choix plus haut).
- Une valeur écrite encodée reste telle quelle : dans un texte d'une extension du tableau, dont le contenu
  est lu tel quel (un message enregistré en `.txt`, une clé dans un `.md`), et dans un texte d'une autre
  extension quand son encodage échappe aux règles des types de fichiers (un bloc de base64 plus court que
  leurs seuils, des octets en hexadécimal écrits avec des chiffres seuls, l'encodage par pourcentage d'une
  adresse). Un texte qui peut contenir de telles valeurs doit être relu avant d'être partagé.
- Une archive restaurée rend ses images sous forme de leurs fichiers texte, sauf si l'archive est celle que
  le service a produite : son original revient alors entier du coffre.
- L'analyse lit quelques dizaines de kilo-octets par seconde sur chaque processeur quand presque chaque mot
  est une donnée personnelle : un fichier texte de plusieurs dizaines de mégaoctets prend des minutes.
- La protection est une pseudonymisation au sens du RGPD : la clé restaure les données, qui restent des
  données personnelles pour son détenteur.
